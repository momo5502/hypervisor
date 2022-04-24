#include <iostream>
#include <conio.h>

#include "std_include.hpp"
#include "driver.hpp"
#include "driver_device.hpp"

#include <irp_data.hpp>

#pragma comment(lib, "Shlwapi.lib")

std::filesystem::path get_current_path()
{
	const auto module = GetModuleHandleA(nullptr);

	char selfdir[MAX_PATH] = {0};
	GetModuleFileNameA(module, selfdir, MAX_PATH);
	PathRemoveFileSpecA(selfdir);

	return selfdir;
}

void patch_data(const driver_device& driver_device, const uint32_t pid, const uint64_t addr, const uint8_t* buffer,
                const size_t length)
{
	hook_request hook_request{};
	hook_request.process_id = pid;
	hook_request.target_address = reinterpret_cast<void*>(addr);

	hook_request.source_data = buffer;
	hook_request.source_data_size = length;

	driver_device::data input{};
	input.assign(reinterpret_cast<uint8_t*>(&hook_request),
	             reinterpret_cast<uint8_t*>(&hook_request) + sizeof(hook_request));

	(void)driver_device.send(HOOK_DRV_IOCTL, input);
}

void insert_nop(const driver_device& driver_device, const uint32_t pid, const uint64_t addr, const size_t length)
{
	std::vector<uint8_t> buffer{};
	buffer.resize(length);
	memset(buffer.data(), 0x90, buffer.size());

	patch_data(driver_device, pid, addr, buffer.data(), buffer.size());
}

void remove_hooks(const driver_device& driver_device)
{
	(void)driver_device.send(UNHOOK_DRV_IOCTL, driver_device::data{});
}

void unsafe_main(const int /*argc*/, char* /*argv*/[])
{
	printf("Pid: %lu\n", GetCurrentProcessId());

	driver driver{get_current_path() / "driver.sys", "MomoLul"};
	const driver_device driver_device{R"(\\.\HelloDev)"};

	std::string pid;
	std::cout << "Please, enter the pid: ";
	std::getline(std::cin, pid);

	int _pid = atoi(pid.data());
	printf("Pid was : %d\n", _pid);

	// IW5
	insert_nop(driver_device, _pid, 0x4488A8, 2); // Force calling CG_DrawFriendOrFoeTargetBoxes
	insert_nop(driver_device, _pid, 0x47F6C7, 2); // Ignore blind-eye perks
	insert_nop(driver_device, _pid, 0x44894C, 2); // Miniconsole

	// T6
	//insert_nop(driver_device, _pid, 0x7B53AE, 6); // Enable chopper boxes
	//insert_nop(driver_device, _pid, 0x7B5461, 6); // Ignore player not visible
	//insert_nop(driver_device, _pid, 0x7B5471, 6); // Ignore blind-eye perks

	//const uint8_t data[] = {0x31, 0xC0, 0xC3};
	//patch_data(driver_device, _pid, 0x4EEFD0, data, sizeof(data));

	//const uint8_t data[] = {0xEB};
	//patch_data(driver_device, _pid, 0x43AE44, data, sizeof(data));

	printf("Press any key to disable all hooks!\n");
	_getch();

	remove_hooks(driver_device);

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
