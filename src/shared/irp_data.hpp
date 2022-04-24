#pragma once

#define HOOK_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)
#define UNHOOK_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)

static_assert(sizeof(void*) == 8);

struct hook_request
{
	uint32_t process_id{};
	const void* target_address{};
	const void* source_data{};
	uint64_t source_data_size{};
};
