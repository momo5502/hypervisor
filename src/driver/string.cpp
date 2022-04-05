#include "std_include.hpp"
#include "string.hpp"

namespace string
{
	_IRQL_requires_max_(DISPATCH_LEVEL)

	UNICODE_STRING get_unicode_string(const wchar_t* string)
	{
		UNICODE_STRING unicode_string{};
		RtlInitUnicodeString(&unicode_string, string);
		return unicode_string;
	}

	char* get_va_buffer()
	{
		constexpr auto va_buffer_count = 0x10;
		static char buffers[va_buffer_count][VA_BUFFER_SIZE];
		static volatile long current_buffer = 0;

		const auto index = InterlockedIncrement(&current_buffer);
		return buffers[index % va_buffer_count];
	}
}
