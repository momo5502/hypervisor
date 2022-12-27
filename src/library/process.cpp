#include "std_include.hpp"
#include "process.hpp"

namespace process
{
	native_handle open(const uint32_t process_id, const DWORD access)
	{
		const auto handle = ::OpenProcess(access, FALSE, process_id);
		if (handle)
		{
			return {handle};
		}

		return {};
	}

	std::vector<HMODULE> get_modules(const native_handle& process)
	{
		if (!process)
		{
			return {};
		}

		DWORD needed = 1024;
		std::vector<HMODULE> result{};

		do
		{
			result.resize(needed);
			if (!EnumProcessModulesEx(process, result.data(), static_cast<DWORD>(result.size()), &needed,
			                          LIST_MODULES_ALL))
			{
				return {};
			}
		}
		while (needed > result.size());

		result.resize(needed);

		// Remove duplicates
		std::ranges::sort(result);
		const auto last = std::ranges::unique(result).begin();
		result.erase(last, result.end());

		// Remove nullptr
		for (auto i = result.begin(); i != result.end();)
		{
			if (*i == nullptr)
			{
				i = result.erase(i);
				break;
			}
			else
			{
				++i;
			}
		}

		return result;
	}

	std::string get_module_filename(const native_handle& process, const HMODULE module)
	{
		if (!process)
		{
			return {};
		}

		std::string buffer{};
		buffer.resize(1024);

		const auto length = GetModuleFileNameExA(process, module, &buffer[0], static_cast<DWORD>(buffer.size()));
		if (length > 0)
		{
			buffer.resize(length);
			return buffer;
		}

		return {};
	}
}
