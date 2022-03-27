#pragma once

class service_handle
{
public:
	service_handle();
	service_handle(SC_HANDLE handle);
	~service_handle();

	service_handle(const service_handle&) = delete;
	service_handle& operator=(const service_handle&) = delete;

	service_handle(service_handle&& obj) noexcept;
	service_handle& operator=(service_handle&& obj) noexcept;

	operator SC_HANDLE() const;

private:
	SC_HANDLE handle_{nullptr};
};
