#include "std_include.hpp"

#include "driver.hpp"
#include "driver_device.hpp"
#include <driver_file.h>
#include <irp_data.hpp>

#include "utils/io.hpp"

#define DLL_IMPORT __declspec(dllexport)
#include <hyperhook.h>

namespace
{
	void patch_data(const driver_device& driver_device, const uint32_t pid, const uint64_t address,
	                const uint8_t* buffer,
	                const size_t length)
	{
		hook_request hook_request{};
		hook_request.process_id = pid;
		hook_request.target_address = reinterpret_cast<void*>(address);

		hook_request.source_data = buffer;
		hook_request.source_data_size = length;

		driver_device::data input{};
		input.assign(reinterpret_cast<uint8_t*>(&hook_request),
		             reinterpret_cast<uint8_t*>(&hook_request) + sizeof(hook_request));

		(void)driver_device.send(HOOK_DRV_IOCTL, input);
	}

	driver_device create_driver_device()
	{
		return driver_device{R"(\\.\HyperHook)"};
	}

	driver create_driver()
	{
		return driver{std::filesystem::absolute(DRIVER_NAME), "HyperHook"};
	}

	driver_device& get_driver_device()
	{
		static driver hypervisor{};
		static driver_device device{};

		if (!hypervisor)
		{
			hypervisor = create_driver();
		}

		if (!device)
		{
			device = create_driver_device();
		}

		return device;
	}
}

int hyperhook_initialize()
{
	try
	{
		const auto& device = get_driver_device();
		if (device)
		{
			return 1;
		}
	}
	catch (const std::exception& e)
	{
		printf("%s\n", e.what());
	}

	return 0;
}

int hyperhook_write(const unsigned int process_id, const unsigned long long address, const void* data,
                    const unsigned long long size)
{
	if (hyperhook_initialize() == 0)
	{
		return 0;
	}

	try
	{
		const auto& device = get_driver_device();
		if (device)
		{
			patch_data(device, process_id, address, static_cast<const uint8_t*>(data), size);
			return 1;
		}
	}
	catch (const std::exception& e)
	{
		printf("%s\n", e.what());
	}

	return 0;
}
