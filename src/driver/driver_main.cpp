#include "std_include.hpp"
#include "logging.hpp"
#include "sleep_callback.hpp"
#include "irp.hpp"
#include "exception.hpp"
#include "hypervisor.hpp"

#define DOS_DEV_NAME L"\\DosDevices\\HelloDev"
#define DEV_NAME L"\\Device\\HelloDev"

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
	}

	~global_driver()
	{
		debug_log("Unloading driver\n");
		this->hypervisor_.disable_all_ept_hooks();
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
