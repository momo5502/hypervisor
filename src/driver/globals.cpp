#include "std_include.hpp"
#include "list.hpp"
#include "globals.hpp"

#include "logging.hpp"

#define _CRTALLOC(x) __declspec(allocate(x))

typedef void (__cdecl* _PVFV)(void);
typedef int (__cdecl* _PIFV)(void);

#pragma section(".CRT$XIA",    long, read) // First C Initializer
#pragma section(".CRT$XIZ",    long, read) // Last C Initializer

#pragma section(".CRT$XTA",    long, read) // First Terminator
#pragma section(".CRT$XTZ",    long, read) // Last Terminator

#pragma section(".CRT$XCA",    long, read) // First C++ Initializer
#pragma section(".CRT$XCZ",    long, read) // Last C++ Initializer

#pragma section(".CRT$XPA",    long, read) // First Pre-Terminator
#pragma section(".CRT$XPZ",    long, read) // Last Pre-Terminator

extern "C" _CRTALLOC(".CRT$XIA") _PIFV __xi_a[] = {nullptr}; // C initializers (first)
extern "C" _CRTALLOC(".CRT$XIZ") _PIFV __xi_z[] = {nullptr}; // C initializers (last)
extern "C" _CRTALLOC(".CRT$XCA") _PVFV __xc_a[] = {nullptr}; // C++ initializers (first)
extern "C" _CRTALLOC(".CRT$XCZ") _PVFV __xc_z[] = {nullptr}; // C++ initializers (last)
extern "C" _CRTALLOC(".CRT$XPA") _PVFV __xp_a[] = {nullptr}; // C pre-terminators (first)
extern "C" _CRTALLOC(".CRT$XPZ") _PVFV __xp_z[] = {nullptr}; // C pre-terminators (last)
extern "C" _CRTALLOC(".CRT$XTA") _PVFV __xt_a[] = {nullptr}; // C terminators (first)
extern "C" _CRTALLOC(".CRT$XTZ") _PVFV __xt_z[] = {nullptr}; // C terminators (last)

namespace globals
{
	namespace
	{
		using destructor = void(*)();
		using destructor_list = utils::list<destructor>;

		destructor_list* destructors = nullptr;

		int run_callbacks(_PIFV* begin, const _PIFV* end)
		{
			int ret = 0;

			while (begin < end && ret == 0)
			{
				if (*begin)
				{
					ret = (**begin)();
				}
				++begin;
			}

			return ret;
		}

		void run_callbacks(_PVFV* begin, const _PVFV* end)
		{
			while (begin < end)
			{
				if (*begin)
				{
					(**begin)();
				}
				++begin;
			}
		}
	}

	void run_constructors()
	{
		if (!destructors)
		{
			destructors = new destructor_list();
		}

		run_callbacks(__xp_a, __xp_z);
		run_callbacks(__xc_a, __xc_z);
	}

	void run_destructors()
	{
		if (!destructors)
		{
			return;
		}

		run_callbacks(__xi_a, __xi_z);
		run_callbacks(__xt_a, __xt_z);

		for (auto* destructor : *destructors)
		{
			try
			{
				destructor();
			}
			catch (const std::exception& e)
			{
				debug_log("Running destructor failed: %s\n", e.what());
			}
		}

		delete destructors;
		destructors = nullptr;
	}
}

int atexit(const globals::destructor destructor)
{
	if (!globals::destructors)
	{
		return 1;
	}

	try
	{
		globals::destructors->push_front(destructor);
	}
	catch (const std::exception& e)
	{
		debug_log("Registering destructor failed: %s\n", e.what());
	}

	return 0;
}
