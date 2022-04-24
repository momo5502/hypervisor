#include "std_include.hpp"
#include "irp.hpp"
#include "finally.hpp"
#include "logging.hpp"
#include "string.hpp"
#include "memory.hpp"

#include <irp_data.hpp>

#include "process.hpp"
#include "thread.hpp"
#include "hypervisor.hpp"

namespace
{
	_Function_class_(DRIVER_DISPATCH) NTSTATUS not_supported_handler(PDEVICE_OBJECT /*device_object*/, const PIRP irp)
	{
		PAGED_CODE()

		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return STATUS_NOT_SUPPORTED;
	}

	_Function_class_(DRIVER_DISPATCH) NTSTATUS success_handler(PDEVICE_OBJECT /*device_object*/, const PIRP irp)
	{
		PAGED_CODE()

		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_SUCCESS;

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return STATUS_SUCCESS;
	}

	vmx::ept_translation_hint* generate_translation_hints(uint32_t process_id, const void* target_address, size_t size)
	{
		vmx::ept_translation_hint* translation_hints{nullptr};

		thread::kernel_thread t([&translation_hints, process_id, target_address, size]
		{
			debug_log("Looking up process: %d\n", process_id);

			const auto process_handle = process::find_process_by_id(process_id);
			if (!process_handle || !process_handle.is_alive())
			{
				debug_log("Bad process\n");
				return;
			}

			const auto name = process_handle.get_image_filename();
			if (name)
			{
				debug_log("Attaching to %s\n", name);
			}

			process::scoped_process_attacher attacher{process_handle};

			debug_log("Generating translation hints for address: %p\n", target_address);
			translation_hints = vmx::ept::generate_translation_hints(target_address, size);
		});

		t.join();

		return translation_hints;
	}

	void apply_hook(const hook_request& request)
	{
		auto* hypervisor = hypervisor::get_instance();
		if (!hypervisor)
		{
			throw std::runtime_error("Hypervisor not installed");
		}

		std::unique_ptr<uint8_t[]> buffer(new uint8_t[request.source_data_size]);
		if (!buffer)
		{
			throw std::runtime_error("Failed to copy buffer");
		}

		vmx::ept_translation_hint* translation_hints = nullptr;
		auto destructor = utils::finally([&translation_hints]()
		{
			vmx::ept::free_translation_hints(translation_hints);
		});

		memcpy(buffer.get(), request.source_data, request.source_data_size);
		translation_hints = generate_translation_hints(request.process_id, request.target_address, request.source_data_size);

		if (!translation_hints)
		{
			debug_log("Failed to generate tranlsation hints\n");
			return;
		}

		hypervisor->install_ept_hook(request.target_address, buffer.get(), request.source_data_size,
		                             translation_hints);
	}

	void unhook()
	{
		const auto instance = hypervisor::get_instance();
		if (instance)
		{
			instance->disable_all_ept_hooks();
		}
	}

	void try_apply_hook(const PIO_STACK_LOCATION irp_sp)
	{
		if (irp_sp->Parameters.DeviceIoControl.InputBufferLength < sizeof(hook_request))
		{
			throw std::runtime_error("Invalid hook request");
		}

		const auto& request = *static_cast<hook_request*>(irp_sp->Parameters.DeviceIoControl.Type3InputBuffer);
		memory::assert_readability(request.source_data, request.source_data_size);
		memory::assert_readability(request.target_address, request.source_data_size);

		apply_hook(request);
	}

	void handle_irp(const PIRP irp)
	{
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

		const auto irp_sp = IoGetCurrentIrpStackLocation(irp);

		if (irp_sp)
		{
			const auto ioctr_code = irp_sp->Parameters.DeviceIoControl.IoControlCode;

			switch (ioctr_code)
			{
			case HELLO_DRV_IOCTL:
				debug_log("Hello from the Driver!\n");
				break;
			case HOOK_DRV_IOCTL:
				try_apply_hook(irp_sp);
				break;
			case UNHOOK_DRV_IOCTL:
				unhook();
				break;
			default:
				debug_log("Invalid IOCTL Code: 0x%X\n", ioctr_code);
				irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
				break;
			}
		}
	}

	_Function_class_(DRIVER_DISPATCH) NTSTATUS io_ctl_handler(
			PDEVICE_OBJECT /*device_object*/, const PIRP irp)
	{
		PAGED_CODE()

		try
		{
			handle_irp(irp);
		}
		catch (std::exception& e)
		{
			debug_log("Handling IRP failed: %s\n", e.what());
			irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		}
		catch (...)
		{
			debug_log("Handling IRP failed\n");
			irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		}

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return irp->IoStatus.Status;
	}
}

irp::irp(const PDRIVER_OBJECT driver_object, const wchar_t* device_name, const wchar_t* dos_device_name)
{
	PAGED_CODE()

	this->device_name_ = string::get_unicode_string(device_name);
	this->dos_device_name_ = string::get_unicode_string(dos_device_name);

	auto destructor = utils::finally([this]()
	{
		if (this->device_object_)
		{
			IoDeleteDevice(this->device_object_);
		}
	});

	auto status = IoCreateDevice(driver_object, 0, &this->device_name_, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
	                             FALSE, &this->device_object_);
	if (!NT_SUCCESS(status))
	{
		throw std::runtime_error("Unable to create device");
	}

	for (auto i = 0u; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		driver_object->MajorFunction[i] = not_supported_handler;
	}

	driver_object->MajorFunction[IRP_MJ_CREATE] = success_handler;
	driver_object->MajorFunction[IRP_MJ_CLOSE] = success_handler;
	driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = io_ctl_handler;

	this->device_object_->Flags |= DO_DIRECT_IO;
	this->device_object_->Flags &= ~DO_DEVICE_INITIALIZING;

	status = IoCreateSymbolicLink(&this->dos_device_name_, &this->device_name_);
	if (!NT_SUCCESS(status))
	{
		throw std::runtime_error("Unable to create symbolic link");
	}

	destructor.cancel();
}

irp::~irp()
{
	try
	{
		PAGED_CODE()

		if (this->device_object_)
		{
			IoDeleteSymbolicLink(&this->dos_device_name_);
			IoDeleteDevice(this->device_object_);
		}
	}
	catch (...)
	{
	}
}

irp::irp(irp&& obj) noexcept
	: irp()
{
	this->operator=(std::move(obj));
}

irp& irp::operator=(irp&& obj) noexcept
{
	if (this != &obj)
	{
		this->~irp();

		this->device_name_ = obj.device_name_;
		this->dos_device_name_ = obj.dos_device_name_;
		this->device_object_ = obj.device_object_;

		obj.device_object_ = nullptr;
	}

	return *this;
}
