#include "std_include.hpp"
#include "memory.hpp"
#include "string.hpp"

namespace memory
{
	namespace
	{
		using mm_allocate_contiguous_node_memory = decltype(MmAllocateContiguousNodeMemory)*;

		mm_allocate_contiguous_node_memory get_mm_allocate_contiguous_node_memory()
		{
			static bool fetched{false};
			static mm_allocate_contiguous_node_memory address{nullptr};

			if (!fetched)
			{
				fetched = true;
				auto function_name = string::get_unicode_string(L"MmAllocateContiguousNodeMemory");
				address = static_cast<mm_allocate_contiguous_node_memory>(MmGetSystemRoutineAddress(&function_name));
			}

			return address;
		}

		void* allocate_aligned_memory_internal(const size_t size)
		{
			PHYSICAL_ADDRESS lowest{}, highest{};
			lowest.QuadPart = 0;
			highest.QuadPart = lowest.QuadPart - 1;

			const auto allocate_node_mem = get_mm_allocate_contiguous_node_memory();
			if (allocate_node_mem)
			{
				return allocate_node_mem(size,
				                         lowest,
				                         highest,
				                         lowest,
				                         PAGE_READWRITE,
				                         KeGetCurrentNodeNumber());
			}

			return MmAllocateContiguousMemory(size, highest);
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

	void* get_physical_address(void* address)
	{
		return reinterpret_cast<void*>(MmGetPhysicalAddress(address).QuadPart);
	}

	void* get_virtual_address(void* address)
	{
		PHYSICAL_ADDRESS physical_address{};
		physical_address.QuadPart = reinterpret_cast<LONGLONG>(address);
		return MmGetVirtualForPhysical(physical_address);
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