#pragma once

void* __cdecl operator new(size_t size, POOL_TYPE pool, unsigned long tag = 'momo');
void* __cdecl operator new[](size_t size, POOL_TYPE pool, unsigned long tag = 'momo');
void* __cdecl operator new(size_t size);
void* __cdecl operator new[](size_t size);

inline void* operator new(size_t, void* where);

void __cdecl operator delete(void *ptr, size_t);
void __cdecl operator delete(void *ptr);
void __cdecl operator delete[](void *ptr, size_t);
void __cdecl operator delete[](void *ptr);
