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
	driver driver{get_current_path() / "driver.sys", "MomoLul"};
	driver_device driver_device{"\\\\.\\HelloDev"};

	driver_device::data input{};
	input.resize(4);

	(void)driver_device.send(HELLO_DRV_IOCTL, input);

	MessageBoxA(0, "Service started!", 0, 0);
	/*
	hook_request hook_request{};
	hook_request.process_id = GetCurrentProcessId();
	hook_request.target_address = "My Message!";

	input.assign(reinterpret_cast<uint8_t*>(&hook_request),
	             reinterpret_cast<uint8_t*>(&hook_request) + sizeof(hook_request));

	(void)driver_device.send(HOOK_DRV_IOCTL, input);

	MessageBoxA(0, "Press ok to exit!", 0, 0);
	*/
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
