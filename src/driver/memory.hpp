#pragma once
#include "type_traits.hpp"

namespace memory
{
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_aligned_memory(void* memory);

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_aligned_memory(size_t size);

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)

	uint64_t get_physical_address(void* address);
	void* get_virtual_address(uint64_t address);

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_non_paged_memory(size_t size);

	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_non_paged_memory(void* memory);

	template <typename T, typename... Args>
	T* allocate_non_paged_object(Args ... args)
	{
		auto* object = static_cast<T*>(allocate_non_paged_memory(sizeof(T)));
		if (object)
		{
			new(object) T(std::forward<Args>(args)...);
		}

		return object;
	}

	template <typename T>
	void free_non_paged_object(T* object)
	{
		if (object)
		{
			object->~T();
			free_non_paged_memory(object);
		}
	}

	template <typename T, typename... Args>
	T* allocate_aligned_object(Args ... args)
	{
		auto* object = static_cast<T*>(allocate_aligned_memory(sizeof(T)));
		if (object)
		{
			new(object) T(std::forward<Args>(args)...);
		}

		return object;
	}

	template <typename T>
	void free_aligned_object(T* object)
	{
		if (object)
		{
			object->~T();
			free_aligned_memory(object);
		}
	}
}

inline uint64_t operator"" _kb(const uint64_t size)
{
	return size * 1024;
}

inline uint64_t operator"" _mb(const uint64_t size)
{
	return operator"" _kb(size * 1024);
}

inline uint64_t operator"" _gb(const uint64_t size)
{
	return operator"" _mb(size * 1024);
}
