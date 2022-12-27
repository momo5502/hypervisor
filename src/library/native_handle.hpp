#pragma once

class native_handle
{
public:
	native_handle();
	native_handle(HANDLE handle);
	~native_handle();

	native_handle(const native_handle&) = delete;
	native_handle& operator=(const native_handle&) = delete;

	native_handle(native_handle&& obj) noexcept;
	native_handle& operator=(native_handle&& obj) noexcept;

	operator HANDLE() const;
	operator bool() const;

private:
	HANDLE handle_{INVALID_HANDLE_VALUE};
};
