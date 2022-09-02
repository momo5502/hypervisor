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

	uint64_t get_physical_address(void* address)
	{
		return static_cast<uint64_t>(MmGetPhysicalAddress(address).QuadPart);
	}

	void* get_virtual_address(const uint64_t address)
	{
		PHYSICAL_ADDRESS physical_address{};
		physical_address.QuadPart = static_cast<LONGLONG>(address);
		return MmGetVirtualForPhysical(physical_address);
	}

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)

	void* map_physical_memory(const uint64_t address, const size_t size)
	{
		PHYSICAL_ADDRESS physical_address{};
		physical_address.QuadPart = static_cast<LONGLONG>(address);
		return MmMapIoSpace(physical_address, size, MmNonCached);
	}

	_IRQL_requires_max_(DISPATCH_LEVEL)

	void unmap_physical_memory(void* address, const size_t size)
	{
		MmUnmapIoSpace(address, size);
	}

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)

	void* allocate_non_paged_memory(const size_t size)
	{
#pragma warning(push)
#pragma warning(disable: 4996)
		void* memory = ExAllocatePoolWithTag(NonPagedPool, size, 'MOMO');
#pragma warning(pop)
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

	bool probe_for_read(const void* address, const size_t length, const uint64_t alignment)
	{
		__try
		{
			ProbeForRead(const_cast<volatile void*>(address), length, static_cast<ULONG>(alignment));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void assert_readability(const void* address, const size_t length, const uint64_t alignment)
	{
		if (!probe_for_read(address, length, alignment))
		{
			throw std::runtime_error("Access violation");
		}
	}

	bool probe_for_write(const void* address, const size_t length, const uint64_t alignment)
	{
		__try
		{
			ProbeForWrite(const_cast<volatile void*>(address), length, static_cast<ULONG>(alignment));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void assert_writability(const void* address, const size_t length, const uint64_t alignment)
	{
		if (!probe_for_write(address, length, alignment))
		{
			throw std::runtime_error("Access violation");
		}
	}
}
