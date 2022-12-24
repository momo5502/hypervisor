#pragma once

#ifdef NDEBUG__
#define debug_log(...)
#else
#define debug_log(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[MOMO] " __VA_ARGS__)
#endif
