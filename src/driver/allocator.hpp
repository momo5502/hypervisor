#pragma once
#include "memory.hpp"

namespace utils
{
	template <typename T>
	concept IsAllocator = requires(size_t size, void* ptr)
	{
		T().free(T().allocate(size));
		T().free(ptr);
	};

	struct AlignedAllocator
	{
		void* allocate(const size_t size)
		{
			return memory::allocate_aligned_memory(size);
		}

		void free(void* ptr)
		{
			memory::free_aligned_memory(ptr);
		}
	};

	struct NonPagedAllocator
	{
		void* allocate(const size_t size)
		{
			return memory::allocate_non_paged_memory(size);
		}

		void free(void* ptr)
		{
			memory::free_non_paged_memory(ptr);
		}
	};
}
