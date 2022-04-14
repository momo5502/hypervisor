#include "std_include.hpp"
#include "logging.hpp"
#include "sleep_callback.hpp"
#include "irp.hpp"
#include "exception.hpp"
#include "hypervisor.hpp"
#include "memory.hpp"
#include "process.hpp"

#define DOS_DEV_NAME L"\\DosDevices\\HelloDev"
#define DEV_NAME L"\\Device\\HelloDev"

namespace
{
	void log_current_process_name()
	{
		const auto process = process::get_current_process();
		const auto* name = process.get_image_filename();

		if (name)
		{
			debug_log("Denied for process: %s\n", name);
		}
	}

	NTSTATUS (*NtCreateFileOrig)(
		PHANDLE FileHandle,
		ACCESS_MASK DesiredAccess,
		POBJECT_ATTRIBUTES ObjectAttributes,
		PIO_STATUS_BLOCK IoStatusBlock,
		PLARGE_INTEGER AllocationSize,
		ULONG FileAttributes,
		ULONG ShareAccess,
		ULONG CreateDisposition,
		ULONG CreateOptions,
		PVOID EaBuffer,
		ULONG EaLength
	);

	NTSTATUS NtCreateFileHook(
		PHANDLE FileHandle,
		ACCESS_MASK DesiredAccess,
		POBJECT_ATTRIBUTES ObjectAttributes,
		PIO_STATUS_BLOCK IoStatusBlock,
		PLARGE_INTEGER AllocationSize,
		ULONG FileAttributes,
		ULONG ShareAccess,
		ULONG CreateDisposition,
		ULONG CreateOptions,
		PVOID EaBuffer,
		ULONG EaLength
	)
	{
		static WCHAR BlockedFileName[] = L"test.txt";
		static SIZE_T BlockedFileNameLength = (sizeof(BlockedFileName) / sizeof(BlockedFileName[0])) - 1;

		PWCH NameBuffer;
		USHORT NameLength;

		__try
		{
			ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
			ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);

			NameBuffer = ObjectAttributes->ObjectName->Buffer;
			NameLength = ObjectAttributes->ObjectName->Length;

			ProbeForRead(NameBuffer, NameLength, 1);

			/* Convert to length in WCHARs */
			NameLength /= sizeof(WCHAR);

			/* Does the file path (ignoring case and null terminator) end with our blocked file name? */
			if (NameLength >= BlockedFileNameLength &&
				_wcsnicmp(&NameBuffer[NameLength - BlockedFileNameLength], BlockedFileName,
				          BlockedFileNameLength) == 0)
			{
				log_current_process_name();
				return STATUS_ACCESS_DENIED;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			NOTHING;
		}

		return NtCreateFileOrig(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
		                        FileAttributes,
		                        ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
	}

	VOID HvEptHookWriteAbsoluteJump(uint8_t* TargetBuffer, SIZE_T TargetAddress)
	{
		/**
		 *   Use 'push ret' instead of 'jmp qword[rip+0]',
		 *   Because 'jmp qword[rip+0]' will read hooked page 8bytes.
		 *
		 *   14 bytes hook:
		 *   0x68 0x12345678 ......................push 'low 32bit of TargetAddress'
		 *   0xC7 0x44 0x24 0x04 0x12345678........mov dword[rsp + 4], 'high 32bit of TargetAddress'
		 *   0xC3..................................ret
		 */

		UINT32 Low32;
		UINT32 High32;

		Low32 = (UINT32)TargetAddress;
		High32 = (UINT32)(TargetAddress >> 32);

		/* push 'low 32bit of TargetAddress' */
		TargetBuffer[0] = 0x68;
		*((UINT32*)&TargetBuffer[1]) = Low32;

		/* mov dword[rsp + 4], 'high 32bit of TargetAddress' */
		*((UINT32*)&TargetBuffer[5]) = 0x042444C7;
		*((UINT32*)&TargetBuffer[9]) = High32;

		/* ret */
		TargetBuffer[13] = 0xC3;
	}

	void* HookCreateFile(hypervisor& hypervisor)
	{
		const uint8_t fixup[] = {
			0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x33, 0xC0, 0x48, 0x89, 0x44, 0x24, 0x78
		};

		auto* target = reinterpret_cast<uint8_t*>(&NtCreateFile);
		if (memcmp(target, fixup, sizeof(fixup)))
		{
			debug_log("Fixup is invalid\n");
			return nullptr;
		}

		auto* trampoline = static_cast<uint8_t*>(memory::allocate_non_paged_memory(sizeof(fixup) + 14));
		if (!trampoline)
		{
			debug_log("Failed to allocate trampoline\n");
			return nullptr;
		}

		memcpy(trampoline, fixup, sizeof(fixup));


		HvEptHookWriteAbsoluteJump(trampoline + sizeof(fixup),
		                           size_t(target) + sizeof(fixup));

		/* Let the hook function call the original function */
		NtCreateFileOrig = reinterpret_cast<decltype(NtCreateFileOrig)>(trampoline);

		/* Write the absolute jump to our shadow page memory to jump to our hook. */
		uint8_t hook[14];
		HvEptHookWriteAbsoluteJump(hook, reinterpret_cast<size_t>(NtCreateFileHook));

		hypervisor.install_ept_hook(target, hook, sizeof(hook));
		return trampoline;
	}
}

class global_driver
{
public:
	global_driver(const PDRIVER_OBJECT driver_object)
		: sleep_callback_([this](const sleep_callback::type type)
		  {
			  this->sleep_notification(type);
		  })
		  , irp_(driver_object, DEV_NAME, DOS_DEV_NAME)
	{
		debug_log("Driver started\n");
		this->trampoline = HookCreateFile(this->hypervisor_);
	}

	~global_driver()
	{
		debug_log("Unloading driver\n");
		this->hypervisor_.disable_all_ept_hooks();
		memory::free_non_paged_memory(this->trampoline);
	}

	global_driver(global_driver&&) noexcept = delete;
	global_driver& operator=(global_driver&&) noexcept = delete;

	global_driver(const global_driver&) = delete;
	global_driver& operator=(const global_driver&) = delete;

	void pre_destroy(const PDRIVER_OBJECT /*driver_object*/)
	{
	}

private:
	bool hypervisor_was_enabled_{false};
	hypervisor hypervisor_{};
	sleep_callback sleep_callback_{};
	irp irp_{};
	void* trampoline{nullptr};

	void sleep_notification(const sleep_callback::type type)
	{
		if (type == sleep_callback::type::sleep)
		{
			debug_log("Going to sleep...\n");
			this->hypervisor_was_enabled_ = this->hypervisor_.is_enabled();
			this->hypervisor_.disable();
		}

		if (type == sleep_callback::type::wakeup && this->hypervisor_was_enabled_)
		{
			debug_log("Waking up...\n");
			this->hypervisor_.enable();
		}
	}
};

global_driver* global_driver_instance{nullptr};

_Function_class_(DRIVER_UNLOAD) void unload(const PDRIVER_OBJECT driver_object)
{
	try
	{
		if (global_driver_instance)
		{
			global_driver_instance->pre_destroy(driver_object);
			delete global_driver_instance;
		}
	}
	catch (std::exception& e)
	{
		debug_log("Destruction error occured: %s\n", e.what());
	}
	catch (...)
	{
		debug_log("Unknown destruction error occured. This should not happen!");
	}
}

extern "C" NTSTATUS DriverEntry(const PDRIVER_OBJECT driver_object, PUNICODE_STRING /*registry_path*/)
{
	try
	{
		driver_object->DriverUnload = unload;
		global_driver_instance = new global_driver(driver_object);
	}
	catch (std::exception& e)
	{
		debug_log("Error: %s\n", e.what());
		return STATUS_INTERNAL_ERROR;
	}
	catch (...)
	{
		debug_log("Unknown initialization error occured");
		return STATUS_INTERNAL_ERROR;
	}

	return STATUS_SUCCESS;
}
