#include <Windows.h>
#include <Shlwapi.h>
#include "finally.hpp"
#include <filesystem>

#pragma comment(lib, "Shlwapi.lib")

#define SERVICE_NAME "MomoLul"

#define HELLO_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)
const char kDevName[] = "\\\\.\\HelloDev";

HANDLE open_device(const char* device_name)
{
	HANDLE device = CreateFileA(device_name,
	                            GENERIC_READ | GENERIC_WRITE,
	                            NULL,
	                            NULL,
	                            OPEN_EXISTING,
	                            NULL,
	                            NULL
	);
	return device;
}

void close_device(HANDLE device)
{
	CloseHandle(device);
}

BOOL send_ioctl(HANDLE device, DWORD ioctl_code)
{
	//prepare input buffer:
	DWORD bufSize = 0x4;
	BYTE* inBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufSize);

	//fill the buffer with some content:
	RtlFillMemory(inBuffer, bufSize, 'A');

	DWORD size_returned = 0;
	BOOL is_ok = DeviceIoControl(device,
	                             ioctl_code,
	                             inBuffer,
	                             bufSize,
	                             NULL, //outBuffer -> None
	                             0, //outBuffer size -> 0
	                             &size_returned,
	                             NULL
	);
	//release the input bufffer:
	HeapFree(GetProcessHeap(), 0, (LPVOID)inBuffer);
	return is_ok;
}

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

		HANDLE dev = open_device(kDevName);
		if (dev == INVALID_HANDLE_VALUE)
		{
			printf("Failed!\n");
			return -1;
		}

		send_ioctl(dev, HELLO_DRV_IOCTL);

		close_device(dev);

		MessageBoxA(0, "Service started!", 0, 0);
	}
	return 0;
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, char*, int)
{
	return main(__argc, __argv);
}
