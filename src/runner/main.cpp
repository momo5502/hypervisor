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

std::optional<uint32_t> get_process_id_from_window(const char* class_name, const char* window_name)
{
	const auto window = FindWindowA(class_name, window_name);
	if (!window)
	{
		return {};
	}

	DWORD process_id{};
	GetWindowThreadProcessId(window, &process_id);
	return static_cast<uint32_t>(process_id);
}

void patch_iw5(const uint32_t pid)
{
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

void try_patch_iw5()
{
	const auto pid = get_process_id_from_window("IW5", nullptr);
	if (pid)
	{
		printf("Patching IW5...\n");
		patch_iw5(*pid);
	}
}

void patch_t6(const uint32_t pid)
{
	// Force calling SatellitePingEnemyPlayer
	insert_nop(pid, 0x7993B1, 2);
	insert_nop(pid, 0x7993C1, 2);

	// Better vsat updates
	insert_nop(pid, 0x41D06C, 2); // No time check
	insert_nop(pid, 0x41D092, 2); // No perk check
	insert_nop(pid, 0x41D0BB, 2); // No fadeout

	// Enable chopper boxes
	insert_nop(pid, 0x7B539C, 6); // ShouldDrawPlayerTargetHighlights
	insert_nop(pid, 0x7B53AE, 6); // Enable chopper boxes
	insert_nop(pid, 0x7B5461, 6); // Ignore player not visible
	insert_nop(pid, 0x7B5471, 6); // Ignore blind-eye perks
}

void try_patch_t6()
{
	const auto pid = get_process_id_from_window(nullptr, "Call of Duty" "\xAE" ": Black Ops II - Multiplayer");
	if (pid)
	{
		printf("Patching T6...\n");
		patch_t6(*pid);
	}
}


int safe_main(const int /*argc*/, char* /*argv*/[])
{
	while (true)
	{
		try_patch_iw5();
		try_patch_t6();

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
