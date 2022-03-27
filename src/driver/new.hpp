#pragma once

void* operator new(size_t size);
void* operator new[](size_t size);

inline void* operator new(size_t, void* where);

void operator delete(void* ptr, size_t);
void operator delete(void* ptr);
void operator delete[](void* ptr, size_t);
void operator delete[](void* ptr);
