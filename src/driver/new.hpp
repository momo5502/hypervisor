#pragma once

namespace std
{
	enum class align_val_t : size_t
	{
	};
}

void* operator new(size_t size);
void* operator new[](size_t size);

void* operator new(size_t, void* where);

void operator delete(void* ptr, size_t);
void operator delete(void* ptr);
void operator delete[](void* ptr, size_t);
void operator delete[](void* ptr);

void operator delete(void* ptr, size_t, std::align_val_t);
void operator delete[](void* ptr, size_t, std::align_val_t);
