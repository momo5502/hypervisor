#pragma once

namespace memory
{
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_aligned_memory(void* memory);

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_aligned_memory(size_t size);

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_non_paged_memory(size_t size);

	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_non_paged_memory(void* memory);
}
