#include "std_include.hpp"
#include "irp.hpp"
#include "finally.hpp"
#include "logging.hpp"
#include "string.hpp"
#include "memory.hpp"

#include <irp_data.hpp>

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

	// TODO: This is vulnerable as fuck. Optimize!
	void apply_hook(hook_request* request)
	{
		const auto address = reinterpret_cast<uint64_t>(request->target_address);
		const auto aligned_address = address & (PAGE_SIZE - 1);
		const auto offset = address - aligned_address;

		debug_log("Original: %s\n", request->target_address);

		static uint8_t buffer[PAGE_SIZE * 2]{0};
		memory::query_process_physical_page(request->process_id, reinterpret_cast<void*>(aligned_address), buffer);

		debug_log("Data: %s\n", buffer + offset);
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
				debug_log("Hello from the Driver!\n");
				break;
			case HOOK_DRV_IOCTL:
				//apply_hook(static_cast<hook_request*>(irp_sp->Parameters.DeviceIoControl.Type3InputBuffer));
				break;
			default:
				debug_log("Invalid IOCTL Code: 0x%X\n", ioctr_code);
				irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
				break;
			}
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
