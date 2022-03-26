#include "std_include.hpp"
#include "logging.hpp"
#include "thread.hpp"
#include "sleep_callback.hpp"

#define DOS_DEV_NAME L"\\DosDevices\\HelloDev"
#define DEV_NAME L"\\Device\\HelloDev"

#define HELLO_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)

_Function_class_(DRIVER_DISPATCH)

NTSTATUS IrpNotImplementedHandler(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	UNREFERENCED_PARAMETER(DeviceObject);
	PAGED_CODE();

	// Complete the request
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_NOT_SUPPORTED;
}

_Function_class_(DRIVER_DISPATCH)

NTSTATUS IrpCreateCloseHandler(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(DeviceObject);
	PAGED_CODE();

	// Complete the request
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

_Function_class_(DRIVER_DISPATCH)
VOID IrpUnloadHandler(IN PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING DosDeviceName = {0};

	PAGED_CODE();

	RtlInitUnicodeString(&DosDeviceName, DOS_DEV_NAME);

	// Delete the symbolic link
	IoDeleteSymbolicLink(&DosDeviceName);

	// Delete the device
	IoDeleteDevice(DriverObject->DeviceObject);

	debug_log("[!] Hello Driver Unloaded\n");
}

_Function_class_(DRIVER_DISPATCH)

NTSTATUS IrpDeviceIoCtlHandler(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	ULONG IoControlCode = 0;
	PIO_STACK_LOCATION IrpSp = NULL;
	NTSTATUS Status = STATUS_NOT_SUPPORTED;

	UNREFERENCED_PARAMETER(DeviceObject);
	PAGED_CODE();

	IrpSp = IoGetCurrentIrpStackLocation(Irp);
	IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;

	if (IrpSp)
	{
		switch (IoControlCode)
		{
		case HELLO_DRV_IOCTL:
			debug_log("[< HelloDriver >] Hello from the Driver!\n");
			break;
		default:
			debug_log("[-] Invalid IOCTL Code: 0x%X\n", IoControlCode);
			Status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
	}

	Irp->IoStatus.Status = Status;
	Irp->IoStatus.Information = 0;

	// Complete the request
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Status;
}

NTSTATUS create_io_device(const PDRIVER_OBJECT DriverObject)
{
	UINT32 i = 0;
	PDEVICE_OBJECT DeviceObject = NULL;
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	UNICODE_STRING DeviceName, DosDeviceName = {0};

	PAGED_CODE();

	RtlInitUnicodeString(&DeviceName, DEV_NAME);
	RtlInitUnicodeString(&DosDeviceName, DOS_DEV_NAME);

	debug_log("[*] In DriverEntry\n");

	// Create the device
	Status = IoCreateDevice(DriverObject,
	                        0,
	                        &DeviceName,
	                        FILE_DEVICE_UNKNOWN,
	                        FILE_DEVICE_SECURE_OPEN,
	                        FALSE,
	                        &DeviceObject);

	if (!NT_SUCCESS(Status))
	{
		if (DeviceObject)
		{
			// Delete the device
			IoDeleteDevice(DeviceObject);
		}

		debug_log("[-] Error Initializing HelloDriver\n");
		return Status;
	}

	// Assign the IRP handlers
	for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		// Disable the Compiler Warning: 28169
#pragma warning(push)
#pragma warning(disable : 28169)
		DriverObject->MajorFunction[i] = IrpNotImplementedHandler;
#pragma warning(pop)
	}

	// Assign the IRP handlers for Create, Close and Device Control
	DriverObject->MajorFunction[IRP_MJ_CREATE] = IrpCreateCloseHandler;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = IrpCreateCloseHandler;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpDeviceIoCtlHandler;

	// Set the flags
	DeviceObject->Flags |= DO_DIRECT_IO;
	DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	// Create the symbolic link
	Status = IoCreateSymbolicLink(&DosDeviceName, &DeviceName);

	// Show the banner
	debug_log("[!] HelloDriver Loaded\n");

	return Status;
}

sleep_callback* sleep_cb{nullptr};

_Function_class_(DRIVER_UNLOAD)

void unload(PDRIVER_OBJECT DriverObject)
{
	debug_log("Leaving World\n");
	IrpUnloadHandler(DriverObject);
	delete sleep_cb;
}

void throw_test()
{
	try
	{
		throw 1;
	}
	catch (...)
	{
		debug_log("Exception caught!\n");
	}
}

extern "C" void __cdecl __std_terminate()
{
	KeBugCheckEx(DRIVER_VIOLATION, 14, 0, 0, 0);
}


extern "C" NTSTATUS DriverEntry(const PDRIVER_OBJECT DriverObject, PUNICODE_STRING /*RegistryPath*/)
{
	DriverObject->DriverUnload = unload;
	debug_log("Hello World\n");

	delete(new int);

	volatile long i = 0;

	thread::dispatch_on_all_cores([&i]()
	{
		const auto index = thread::get_processor_index();
		while (i != index)
		{
		}

		debug_log("Hello from CPU %u/%u\n", thread::get_processor_index() + 1, thread::get_processor_count());
		InterlockedIncrement(&i);
	});

	debug_log("Final i = %i\n", i);

	throw_test();

	try
	{
		sleep_cb = new sleep_callback([](const sleep_callback::type type)
		{ 
			if (type == sleep_callback::type::sleep)
			{
				debug_log("Going to sleep!");
			}

			if (type == sleep_callback::type::wakeup)
			{
				debug_log("Waking up!");
			}
		});

		sleep_cb->dispatcher(sleep_callback::type::sleep);
		sleep_cb->dispatcher(sleep_callback::type::wakeup);
	}
	catch (...)
	{
		debug_log("Failed to register sleep callback");
	}

	return create_io_device(DriverObject);

	//return STATUS_SUCCESS;
}
