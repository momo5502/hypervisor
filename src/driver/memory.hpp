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

	uint64_t query_process_physical_page(uint32_t process_id, void* address, uint8_t buffer[PAGE_SIZE]);

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
