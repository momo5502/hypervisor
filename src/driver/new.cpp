#include "std_include.hpp"
#include "new.hpp"
#include "exception.hpp"
#include "memory.hpp"

namespace
{
	void* perform_checked_non_paged_allocation(const size_t size)
	{
		auto* memory = memory::allocate_non_paged_memory(size);
		if (!memory)
		{
			throw std::runtime_error("Memory allocation failed");
		}

		return memory;
	}
}

void* operator new(const size_t size)
{
	return perform_checked_non_paged_allocation(size);
}

void* operator new[](const size_t size)
{
	return perform_checked_non_paged_allocation(size);
}

//	Placement new
void* operator new(size_t, void* where)
{
	return where;
}

void operator delete(void* ptr, size_t)
{
	memory::free_non_paged_memory(ptr);
}

void operator delete(void* ptr)
{
	memory::free_non_paged_memory(ptr);
}

void operator delete[](void* ptr, size_t)
{
	memory::free_non_paged_memory(ptr);
}

void operator delete[](void* ptr)
{
	memory::free_non_paged_memory(ptr);
}

void operator delete(void*, size_t, std::align_val_t)
{
}

void operator delete[](void*, size_t, std::align_val_t)
{
}

extern "C" void __std_terminate()
{
	KeBugCheckEx(DRIVER_VIOLATION, 14, 0, 0, 0);
}
