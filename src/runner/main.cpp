#include <Windows.h>
#include <Shlwapi.h>
#include "finally.hpp"
#include <filesystem>

#pragma comment(lib, "Shlwapi.lib")

#define SERVICE_NAME "MomoLul"

std::filesystem::path get_current_path()
{
	const auto module = GetModuleHandleA(nullptr);

	char selfdir[MAX_PATH] = {0};
	GetModuleFileNameA(module, selfdir, MAX_PATH);
	PathRemoveFileSpecA(selfdir);

	return selfdir;
}

int main(const int /*argc*/, char* /*argv*/[])
{
	const auto manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (manager == nullptr)
	{
		return 1;
	}

	const auto _1 = utils::finally([&manager]()
	{
		CloseServiceHandle(manager);
	});

	auto service = OpenServiceA(manager, SERVICE_NAME, SERVICE_ALL_ACCESS);
	const auto _2 = utils::finally([&service]()
	{
		if (service)
		{
			SERVICE_STATUS status;
			ControlService(service, SERVICE_CONTROL_STOP, &status);

			DeleteService(service);
			CloseServiceHandle(service);
		}
	});

	if (service == nullptr)
	{
		const auto driver_path = get_current_path() / "driver.sys";

		service = CreateServiceA(manager, SERVICE_NAME,
		                         SERVICE_NAME, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
		                         SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
		                         driver_path.generic_string().data(), nullptr, nullptr,
		                         nullptr, nullptr, nullptr);
	}

	if (service == nullptr)
	{
		service = OpenServiceA(manager, SERVICE_NAME,
		                       SERVICE_ALL_ACCESS);
	}

	if (service)
	{
		StartServiceA(service, 0, nullptr);
		MessageBoxA(0, "Service started!", 0, 0);
	}
	return 0;
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, char*, int)
{
	return main(__argc, __argv);
}
