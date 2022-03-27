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
}
