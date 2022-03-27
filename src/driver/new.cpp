#include "std_include.hpp"
#include "new.hpp"
#include "exception.hpp"

namespace
{
	void* perform_allocation(const size_t size, const POOL_TYPE pool, const unsigned long tag)
	{
		auto* memory = ExAllocatePoolWithTag(pool, size, tag);
		if (!memory)
		{
			throw std::runtime_error("Memory allocation failed");
		}

		return memory;
	}
}

void* operator new(const size_t size, const POOL_TYPE pool, const unsigned long tag)
{
	return perform_allocation(size, pool, tag);
}

void* operator new[](const size_t size, const POOL_TYPE pool, const unsigned long tag)
{
	return perform_allocation(size, pool, tag);
}

void* operator new(const size_t size)
{
	return operator new(size, NonPagedPool);
}

void* operator new[](const size_t size)
{
	return operator new[](size, NonPagedPool);
}

//	Placement new
void* operator new(size_t, void* where)
{
	return where;
}

void operator delete(void* ptr, size_t)
{
	ExFreePool(ptr);
}

void operator delete(void* ptr)
{
	ExFreePool(ptr);
}

void operator delete[](void* ptr, size_t)
{
	ExFreePool(ptr);
}

void operator delete[](void* ptr)
{
	ExFreePool(ptr);
}

extern "C" void __std_terminate()
{
	KeBugCheckEx(DRIVER_VIOLATION, 14, 0, 0, 0);
}
