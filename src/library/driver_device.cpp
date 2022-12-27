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
	size_t out_len = output.size();
	if (this->send(ioctl_code, input.data(), input.size(), output.data(), &out_len))
	{
		output.resize(out_len);
		return true;
	}

	return false;
}

bool driver_device::send(const DWORD ioctl_code, const void* input, const size_t input_length, void* output,
                         size_t* output_length) const
{
	DWORD size_returned = 0;
	const auto success = DeviceIoControl(this->device_,
	                                     ioctl_code,
	                                     const_cast<void*>(input),
	                                     static_cast<DWORD>(input_length),
	                                     output,
	                                     static_cast<DWORD>(*output_length),
	                                     &size_returned,
	                                     nullptr
	) != FALSE;

	*output_length = 0;
	if (success)
	{
		*output_length = size_returned;
	}

	return success;
}
