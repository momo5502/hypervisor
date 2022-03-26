#include "irp.hpp"
#include "finally.hpp"
#include "logging.hpp"
#include "exception.hpp"

#define DOS_DEV_NAME L"\\DosDevices\\HelloDev"
#define DEV_NAME L"\\Device\\HelloDev"

#define HELLO_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)

namespace irp
{
	namespace
	{
		UNICODE_STRING get_unicode_string(const wchar_t* string)
		{
			UNICODE_STRING unicode_string{};
			RtlInitUnicodeString(&unicode_string, string);
			return unicode_string;
		}

		UNICODE_STRING get_device_name()
		{
			return get_unicode_string(DEV_NAME);
		}

		UNICODE_STRING get_dos_device_name()
		{
			return get_unicode_string(DOS_DEV_NAME);
		}

		_Function_class_(DRIVER_DISPATCH) NTSTATUS not_supported_handler(
			PDEVICE_OBJECT /*device_object*/, const PIRP irp)
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

		_Function_class_(DRIVER_DISPATCH) NTSTATUS io_ctl_handler(
			PDEVICE_OBJECT /*device_object*/, const PIRP irp)
		{
			PAGED_CODE()

			irp->IoStatus.Information = 0;
			irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

			const auto irp_sp = IoGetCurrentIrpStackLocation(irp);

			if (irp_sp)
			{
				const auto ioctr_code = irp_sp->Parameters.DeviceIoControl.IoControlCode;

				switch (ioctr_code)
				{
				case HELLO_DRV_IOCTL:
					debug_log("[< HelloDriver >] Hello from the Driver!\n");
					break;
				default:
					debug_log("[-] Invalid IOCTL Code: 0x%X\n", ioctr_code);
					irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
					break;
				}
			}

			IoCompleteRequest(irp, IO_NO_INCREMENT);

			return irp->IoStatus.Status;
		}
	}

	_Function_class_(DRIVER_DISPATCH) void uninitialize(const PDRIVER_OBJECT driver_object)
	{
		PAGED_CODE()

		auto dos_device_name = get_dos_device_name();

		IoDeleteSymbolicLink(&dos_device_name);
		IoDeleteDevice(driver_object->DeviceObject);
	}

	void initialize(const PDRIVER_OBJECT driver_object)
	{
		PAGED_CODE()

		auto device_name = get_device_name();
		auto dos_device_name = get_dos_device_name();

		PDEVICE_OBJECT device_object{};
		auto destructor = utils::finally([&device_object]()
		{
			if (device_object)
			{
				IoDeleteDevice(device_object);
			}
		});

		auto status = IoCreateDevice(driver_object, 0, &device_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
		                             FALSE, &device_object);
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

		device_object->Flags |= DO_DIRECT_IO;
		device_object->Flags &= ~DO_DEVICE_INITIALIZING;

		status = IoCreateSymbolicLink(&dos_device_name, &device_name);
		if (!NT_SUCCESS(status))
		{
			throw std::runtime_error("Unable to create symbolic link");
		}

		destructor.cancel();
	}
}
