#include <iostream>
#include <conio.h>

#include "std_include.hpp"
#include "finally.hpp"
#include "driver.hpp"
#include "driver_device.hpp"

#include <irp_data.hpp>

#pragma comment(lib, "Shlwapi.lib")

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

void unsafe_main(const int /*argc*/, char* /*argv*/[])
{
	printf("Pid: %lu\n", GetCurrentProcessId());

	driver driver{get_current_path() / "driver.sys", "MomoLul"};
	driver_device driver_device{"\\\\.\\HelloDev"};

	driver_device::data input{};
	input.resize(4);

	(void)driver_device.send(HELLO_DRV_IOCTL, input);

	std::string pid;

	std::cout << "Please, enter the pid: ";
	std::getline(std::cin, pid);

	int _pid = atoi(pid.data());
	printf("Pid was : %d\n", _pid);

	hook_request hook_request{};
	hook_request.process_id = _pid; //GetCurrentProcessId();
	hook_request.target_address = (void*)0x4488A8;//0x41297A;

	uint8_t buffer[] = {0x90, 0x90};
	//uint8_t buffer[] = { 0x48, 0x89, 0xD9, 0x90 };

	hook_request.source_data = buffer;
	hook_request.source_data_size = sizeof(buffer);

	input.assign(reinterpret_cast<uint8_t*>(&hook_request),
	             reinterpret_cast<uint8_t*>(&hook_request) + sizeof(hook_request));

	(void)driver_device.send(HOOK_DRV_IOCTL, input);

	printf("Press any key to disable all hooks!\n");
	_getch();

	input.resize(0);
	(void)driver_device.send(UNHOOK_DRV_IOCTL, input);

	printf("Press any key to exit!\n");
	_getch();
}

int main(const int argc, char* argv[])
{
	try
	{
		unsafe_main(argc, argv);
		return 0;
	}
	catch (std::exception& e)
	{
		printf("Error: %s\n", e.what());
		return 1;
	}
	catch (...)
	{
		printf("An unknown error occured!\n");
		return 1;
	}
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, char*, int)
{
	return main(__argc, __argv);
}
