#pragma once

#ifdef NDEBUG__
#define debug_log(...)
#else
#define debug_log(msg, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MOMO] " msg, __VA_ARGS__)
#endif
