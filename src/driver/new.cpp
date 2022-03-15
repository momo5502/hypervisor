#include "std_include.hpp"
#include "new.hpp"

void* __cdecl operator new(const size_t size, const POOL_TYPE pool, const unsigned long tag)
{
	return ExAllocatePoolWithTag(pool, size, tag);
}

void* __cdecl operator new[](const size_t size, const POOL_TYPE pool, const unsigned long tag)
{
	return ExAllocatePoolWithTag(pool, size, tag);
}

void* __cdecl operator new(const size_t size)
{
	return operator new(size, NonPagedPool);
}

void* __cdecl operator new[](const size_t size)
{
	return operator new[](size, NonPagedPool);
}

//	Placement new
inline void* operator new(size_t, void* where)
{
	return where;
}

void __cdecl operator delete(void* ptr, size_t)
{
	ExFreePool(ptr);
}

void __cdecl operator delete(void* ptr)
{
	ExFreePool(ptr);
}

void __cdecl operator delete[](void* ptr, size_t)
{
	ExFreePool(ptr);
}

void __cdecl operator delete[](void* ptr)
{
	ExFreePool(ptr);
}
