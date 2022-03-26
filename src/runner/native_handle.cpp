#include "std_include.hpp"
#include "native_handle.hpp"

native_handle::native_handle()
	: native_handle(INVALID_HANDLE_VALUE)
{
}

native_handle::native_handle(const HANDLE handle)
	: handle_{handle}
{
}

native_handle::~native_handle()
{
	if (this->operator bool())
	{
		CloseHandle(this->handle_);
		this->handle_ = INVALID_HANDLE_VALUE;
	}
}

native_handle::native_handle(native_handle&& obj) noexcept
	: native_handle()
{
	this->operator=(std::move(obj));
}

native_handle& native_handle::operator=(native_handle&& obj) noexcept
{
	if (this != &obj)
	{
		this->~native_handle();
		this->handle_ = obj.handle_;
		obj.handle_ = INVALID_HANDLE_VALUE;
	}

	return *this;
}

native_handle::operator HANDLE() const
{
	return this->handle_;
}

native_handle::operator bool() const
{
	return this->handle_ != INVALID_HANDLE_VALUE;
}