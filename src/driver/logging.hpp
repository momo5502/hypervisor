#pragma once

#ifdef NDEBUG
#define debug_log(...)
#else
#define debug_log(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__)
#endif
