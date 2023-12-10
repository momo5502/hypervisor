#include "std_include.hpp"
#include "driver.hpp"

driver::driver(const std::filesystem::path& driver_file, const std::string& service_name)
{
	this->manager_ = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (!this->manager_)
	{
		throw std::runtime_error("Unable to open SC manager");
	}

	this->service_ = OpenServiceA(this->manager_, service_name.data(), SERVICE_ALL_ACCESS);

	if (!this->service_)
	{
		this->service_ = CreateServiceA(this->manager_, service_name.data(),
		                                service_name.data(), SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
		                                SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
		                                driver_file.generic_string().data(), nullptr, nullptr,
		                                nullptr, nullptr, nullptr);
	}

	if (!this->service_)
	{
		this->service_ = OpenServiceA(this->manager_, service_name.data(), SERVICE_ALL_ACCESS);
	}

	if (!this->service_)
	{
		this->manager_ = {};
		throw std::runtime_error("Unable to create service");
	}

	if(!StartServiceA(this->service_, 0, nullptr))
	{
		printf("Failed to start service: %d\n", GetLastError());
	}
}

driver::~driver()
{
	if (this->service_)
	{
		SERVICE_STATUS status{};
		ControlService(this->service_, SERVICE_CONTROL_STOP, &status);

		DeleteService(this->service_);
	}
}
