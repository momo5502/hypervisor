#pragma once
#include "type_traits.hpp"

#define VA_BUFFER_SIZE 0x1000

namespace string
{
	_IRQL_requires_max_(DISPATCH_LEVEL)
	UNICODE_STRING get_unicode_string(const wchar_t* string);

	char* get_va_buffer();

	template <typename... Args>
	const char* va(const char* message, Args&&... args)
	{
		auto* buffer = get_va_buffer();
		RtlStringCchPrintfA(buffer, VA_BUFFER_SIZE, message, std::forward<Args>(args)...);
		return buffer;
	}

	inline bool equal(const char* s1, const char* s2)
	{
		if (!s1)
		{
			return !s2;
		}

		while(*s1)
		{
			if(*(s1++) != *(s2++))
			{
				return false;
			}
		}

		return !*s2;
	}
}
