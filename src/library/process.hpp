#pragma once
#include "native_handle.hpp"

namespace process
{
	native_handle open(uint32_t process_id, DWORD access);
	std::vector<HMODULE> get_modules(const native_handle& process);
	std::string get_module_filename(const native_handle& process, HMODULE module);
}
