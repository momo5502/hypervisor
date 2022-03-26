#include "std_include.hpp"
#include "logging.hpp"
#include "sleep_callback.hpp"
#include "irp.hpp"
#include "exception.hpp"
#include "finally.hpp"

sleep_callback* sleep_cb{nullptr};

void sleep_notification(const sleep_callback::type type)
{
	if (type == sleep_callback::type::sleep)
	{
		debug_log("Going to sleep!");
	}

	if (type == sleep_callback::type::wakeup)
	{
		debug_log("Waking up!");
	}
}

extern "C" void __cdecl __std_terminate()
{
	KeBugCheckEx(DRIVER_VIOLATION, 14, 0, 0, 0);
}

void destroy_sleep_callback()
{
	delete sleep_cb;
}

_Function_class_(DRIVER_UNLOAD) void unload(const PDRIVER_OBJECT driver_object)
{
	irp::uninitialize(driver_object);
	destroy_sleep_callback();
}

extern "C" NTSTATUS DriverEntry(const PDRIVER_OBJECT driver_object, PUNICODE_STRING /*registry_path*/)
{
	driver_object->DriverUnload = unload;

	auto sleep_destructor = utils::finally(&destroy_sleep_callback);

	try
	{
		sleep_cb = new sleep_callback(sleep_notification);
		irp::initialize(driver_object);
		sleep_destructor.cancel();
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
