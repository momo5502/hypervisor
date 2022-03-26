#pragma once
#include "service_handle.hpp"

class driver
{
public:
	driver(const std::filesystem::path& driver_file, const std::string& service_name);
	~driver();
	
	driver(const driver&) = delete;
	driver& operator=(const driver&) = delete;

	driver(driver&& obj) noexcept = default;;
	driver& operator=(driver&& obj) noexcept = default;

private:
	service_handle manager_{};
	service_handle service_{};
};
