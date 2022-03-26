#pragma once

class hypervisor
{
public:
	hypervisor();
	~hypervisor();

	hypervisor(hypervisor&& obj) noexcept = delete;
	hypervisor& operator=(hypervisor&& obj) noexcept = delete;

	hypervisor(const hypervisor& obj) = delete;
	hypervisor& operator=(const hypervisor& obj) = delete;

	void on_sleep();
	void on_wakeup();

private:
};