#pragma once

class irp
{
public:
	irp() = default;
	irp(PDRIVER_OBJECT driver_object, const wchar_t* device_name, const wchar_t* dos_device_name);
	~irp();

	irp(irp&& obj) noexcept;
	irp& operator=(irp&& obj) noexcept;

	irp(const irp&) = delete;
	irp& operator=(const irp&) = delete;

private:
	UNICODE_STRING device_name_{};
	UNICODE_STRING dos_device_name_{};
	PDEVICE_OBJECT device_object_{};
};
