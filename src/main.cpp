#include "std_include.hpp"
#include "logging.hpp"

#include "thread.hpp"

_Function_class_(DRIVER_UNLOAD)

void unload(PDRIVER_OBJECT /*DriverObject*/)
{
	debug_log("Leaving World\n");
}

extern "C" NTSTATUS DriverEntry(const PDRIVER_OBJECT DriverObject, PUNICODE_STRING /*RegistryPath*/)
{
	DriverObject->DriverUnload = unload;
	debug_log("Hello World\n");

	volatile long i = 0;

	thread::dispatch_on_all_cores([&i]()
	{
		const auto index = thread::get_processor_index();
		while (i != index)
		{
		}

		debug_log("Hello from CPU %u/%u\n", thread::get_processor_index() + 1, thread::get_processor_count());
		++i;
	});

	debug_log("Final i = %i\n", i);

	return STATUS_SUCCESS;
}
