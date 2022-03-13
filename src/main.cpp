#include <ntddk.h>
#include "logging.h"
#include "nt_ext.h"

_Function_class_(KDEFERRED_ROUTINE)

void NTAPI test_function(struct _KDPC* /*Dpc*/,
                         PVOID param,
                         const PVOID arg1,
                         const PVOID arg2)
{
	const auto core_id = KeGetCurrentProcessorNumberEx(nullptr);
	DbgLog("%s from CPU %u\n", static_cast<const char*>(param), core_id);

	KeSignalCallDpcSynchronize(arg2);
	KeSignalCallDpcDone(arg1);
}


_Function_class_(DRIVER_UNLOAD)
void unload(PDRIVER_OBJECT /*DriverObject*/)
{
	DbgLog("Leaving World\n");
	KeGenericCallDpc(test_function, "Bye");
	DbgLog("Bye World\n");
}

extern "C" {

NTSTATUS DriverEntry(const PDRIVER_OBJECT DriverObject, PUNICODE_STRING /*RegistryPath*/)
{
	DriverObject->DriverUnload = unload;

	DbgLog("Hello World\n");

	KeGenericCallDpc(test_function, "Hello");

	DbgLog("Nice World\n");

	return STATUS_SUCCESS;
}

}
