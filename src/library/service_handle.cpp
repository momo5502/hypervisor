#include "std_include.hpp"
#include "service_handle.hpp"

service_handle::service_handle()
	: service_handle(nullptr)
{
}

service_handle::service_handle(const SC_HANDLE handle)
	: handle_{handle}
{
}

service_handle::~service_handle()
{
	if (this->handle_)
	{
		CloseServiceHandle(this->handle_);
		this->handle_ = nullptr;
	}
}

service_handle::service_handle(service_handle&& obj) noexcept
	: service_handle()
{
	this->operator=(std::move(obj));
}

service_handle& service_handle::operator=(service_handle&& obj) noexcept
{
	if (this != &obj)
	{
		this->~service_handle();
		this->handle_ = obj.handle_;
		obj.handle_ = nullptr;
	}

	return *this;
}

service_handle::operator SC_HANDLE() const
{
	return this->handle_;
}
