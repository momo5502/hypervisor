#include <iostream>
#include <filesystem>
#include <conio.h>
#include <fstream>

#include "std_include.hpp"
#include "driver.hpp"
#include "driver_device.hpp"

#include <irp_data.hpp>

#include "resource.hpp"

#pragma comment(lib, "Shlwapi.lib")

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

std::vector<uint8_t> load_resource(const int id)
{
	auto* const res = FindResource(GetModuleHandleA(nullptr), MAKEINTRESOURCE(id), RT_RCDATA);
	if (!res) return {};

	auto* const handle = LoadResource(nullptr, res);
	if (!handle) return {};

	const auto* data_ptr = static_cast<uint8_t*>(LockResource(handle));
	const auto data_size = SizeofResource(nullptr, res);

	std::vector<uint8_t> data{};
	data.assign(data_ptr, data_ptr + data_size);
	return data;
}

std::filesystem::path extract_driver()
{
	const auto data = load_resource(DRIVER_BINARY);

	auto driver_file = std::filesystem::temp_directory_path() / "driver.sys";

	std::ofstream out_file{};
	out_file.open(driver_file.generic_string(), std::ios::out | std::ios::binary);
	out_file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	out_file.close();

	return driver_file;
}

void unsafe_main(const int /*argc*/, char* /*argv*/[])
{
	const auto driver_file = extract_driver();

	driver driver{driver_file, "MomoLul"};
	const driver_device driver_device{R"(\\.\HelloDev)"};

	//launcher().run();

	std::string pid_str{};
	printf("Please enter the pid: ");
	std::getline(std::cin, pid_str);

	const auto pid = atoi(pid_str.data());

	/*
	// IW5
	insert_nop(driver_device, pid, 0x4488A8, 2); // Force calling CG_DrawFriendOrFoeTargetBoxes
	insert_nop(driver_device, pid, 0x47F6C7, 2); // Ignore blind-eye perks
	//insert_nop(driver_device, pid, 0x44894C, 2); // Miniconsole

	// Always full alpha
	constexpr uint8_t data1[] = {0xD9, 0xE8, 0xC3};
	patch_data(driver_device, pid, 0x47F0D0, data1, sizeof(data1));

	// Compass show enemies
	constexpr uint8_t data2[] = {0xEB, 0x13};
	patch_data(driver_device, pid, 0x4437A8, data2, sizeof(data2));

	// Enemy arrows
	constexpr uint8_t data3[] = {0xEB};
	patch_data(driver_device, pid, 0x443A2A, data3, sizeof(data3));
	patch_data(driver_device, pid, 0x443978, data3, sizeof(data3));
	*/

	/*
	insert_nop(driver_device, pid, 0x441D5A, 6);
	insert_nop(driver_device, pid, 0x525104, 2);
	insert_nop(driver_device, pid, 0x525121, 2);

	constexpr uint8_t data3[] = {0xEB};
	patch_data(driver_device, pid, 0x525087, data3, sizeof(data3));
	patch_data(driver_device, pid, 0x524E7F, data3, sizeof(data3));
	patch_data(driver_device, pid, 0x52512C, data3, sizeof(data3));
	*/

	printf("Press any key to disable all hooks!\n");
	(void)_getch();

	remove_hooks(driver_device);

	printf("Press any key to exit!\n");
	(void)_getch();
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
		_getch();
		return 1;
	}
	catch (...)
	{
		printf("An unknown error occured!\n");
		_getch();
		return 1;
	}
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, char*, int)
{
	AllocConsole();
	AttachConsole(GetCurrentProcessId());

	FILE* fp;
	freopen_s(&fp, "conin$", "r", stdin);
	freopen_s(&fp, "conout$", "w", stdout);
	freopen_s(&fp, "conout$", "w", stderr);

	return main(__argc, __argv);
}
