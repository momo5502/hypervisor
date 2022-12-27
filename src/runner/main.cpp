#include "std_include.hpp"

#include <iostream>
#include <conio.h>
#include <set>

#include "resource.hpp"

extern "C" __declspec(dllimport)
int hyperhook_write(unsigned int process_id, unsigned long long address, const void* data,
                    unsigned long long size);

bool patch_data(const uint32_t process_id, const uint64_t address, const void* buffer,
                const size_t length)
{
	return hyperhook_write(process_id, address, buffer, length) != 0;
}

bool insert_nop(const uint32_t process_id, const uint64_t address, const size_t length)
{
	std::vector<uint8_t> buffer{};
	buffer.resize(length);
	memset(buffer.data(), 0x90, buffer.size());

	return patch_data(process_id, address, buffer.data(), buffer.size());
}

uint32_t get_process_id()
{
	std::string pid_str{};
	printf("Please enter the pid: ");
	std::getline(std::cin, pid_str);

	return atoi(pid_str.data());
}

void activate_patches(const uint32_t pid)
{
	// IW5
	insert_nop(pid, 0x4488A8, 2); // Force calling CG_DrawFriendOrFoeTargetBoxes
	insert_nop(pid, 0x47F6C7, 2); // Ignore blind-eye perks
	//insert_nop(driver_device, pid, 0x44894C, 2); // Miniconsole

	// Always full alpha
	constexpr uint8_t data1[] = {0xD9, 0xE8, 0xC3};
	patch_data(pid, 0x47F0D0, data1, sizeof(data1));

	// Compass show enemies
	constexpr uint8_t data2[] = {0xEB, 0x13};
	patch_data(pid, 0x4437A8, data2, sizeof(data2));

	// Enemy arrows
	constexpr uint8_t data3[] = {0xEB};
	patch_data(pid, 0x443A2A, data3, sizeof(data3));
	patch_data(pid, 0x443978, data3, sizeof(data3));
}

int safe_main(const int /*argc*/, char* /*argv*/[])
{
	const auto pid = get_process_id();

	while (true)
	{
		activate_patches(pid);

		printf("Press any key to exit!\n");
		if (_getch() != 'r')
		{
			break;
		}
	}

	return 0;
}

int main(const int argc, char* argv[])
{
	try
	{
		return safe_main(argc, argv);
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
