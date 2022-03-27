#include "std_include.hpp"
#include "memory.hpp"

namespace memory
{
	namespace
	{
		void* allocate_aligned_memory_internal(const size_t size)
		{
			PHYSICAL_ADDRESS lowest{}, highest{};
			lowest.QuadPart = 0;
			highest.QuadPart = lowest.QuadPart - 1;

#if (NTDDI_VERSION >= NTDDI_VISTA)
			return MmAllocateContiguousNodeMemory(size,
			                                      lowest,
			                                      highest,
			                                      lowest,
			                                      PAGE_READWRITE,
			                                      KeGetCurrentNodeNumber());
#else
		return MmAllocateContiguousMemory(size, highest);
#endif
		}
	}

	_IRQL_requires_max_(DISPATCH_LEVEL)

	void free_aligned_memory(void* memory)
	{
		if (memory)
		{
			MmFreeContiguousMemory(memory);
		}
	}

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)

	void* allocate_aligned_memory(const size_t size)
	{
		void* memory = allocate_aligned_memory_internal(size);
		if (memory)
		{
			RtlSecureZeroMemory(memory, size);
		}

		return memory;
	}

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)

	void* allocate_non_paged_memory(const size_t size)
	{
		void* memory = ExAllocatePoolWithTag(NonPagedPool, size, 'MOMO');
		if (memory)
		{
			RtlSecureZeroMemory(memory, size);
		}

		return memory;
	}

	_IRQL_requires_max_(DISPATCH_LEVEL)

	void free_non_paged_memory(void* memory)
	{
		if (memory)
		{
			ExFreePool(memory);
		}
	}
}
