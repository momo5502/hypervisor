#pragma once

namespace string
{
	_IRQL_requires_max_(DISPATCH_LEVEL)
	UNICODE_STRING get_unicode_string(const wchar_t* string);
}
