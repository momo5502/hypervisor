#pragma once
#include "native_handle.hpp"

class driver_device
{
public:
	driver_device(const std::string& driver_device);
	~driver_device() = default;

	driver_device(const driver_device&) = delete;
	driver_device& operator=(const driver_device&) = delete;

	driver_device(driver_device&& obj) noexcept = default;
	driver_device& operator=(driver_device&& obj) noexcept = default;

	using data = std::vector<uint8_t>;
	bool send(DWORD ioctl_code, const data& input) const;
	bool send(DWORD ioctl_code, const data& input, data& output) const;

private:
	native_handle device_{};
};
