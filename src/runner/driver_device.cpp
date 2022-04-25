#include "std_include.hpp"
#include "driver_device.hpp"

driver_device::driver_device(const std::string& driver_device)
{
	this->device_ = CreateFileA(driver_device.data(),
	                            GENERIC_READ | GENERIC_WRITE,
	                            NULL,
	                            nullptr,
	                            OPEN_EXISTING,
	                            NULL,
	                            nullptr);

	if (!this->device_)
	{
		throw std::runtime_error("Unable to access device");
	}
}

bool driver_device::send(const DWORD ioctl_code, const data& input) const
{
	data output{};
	return this->send(ioctl_code, input, output);
}

bool driver_device::send(const DWORD ioctl_code, const data& input, data& output) const
{
	DWORD size_returned = 0;
	const auto success = DeviceIoControl(this->device_,
	                                     ioctl_code,
	                                     const_cast<uint8_t*>(input.data()),
	                                     static_cast<DWORD>(input.size()),
	                                     output.data(),
	                                     static_cast<DWORD>(output.size()),
	                                     &size_returned,
	                                     nullptr
	) != FALSE;

	if (success && size_returned < output.size())
	{
		output.resize(size_returned);
	}

	return success;
}
