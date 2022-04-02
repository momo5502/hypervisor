#pragma once

namespace thread
{
	uint32_t get_processor_count();
	uint32_t get_processor_index();

	_IRQL_requires_min_(PASSIVE_LEVEL)
	_IRQL_requires_max_(APC_LEVEL)
	bool sleep(uint32_t milliseconds);

	_IRQL_requires_max_(APC_LEVEL)
	_IRQL_requires_min_(PASSIVE_LEVEL)
	_IRQL_requires_same_
	void dispatch_on_all_cores(void (*callback)(void*), void* data, bool sequential = false);

	_IRQL_requires_max_(APC_LEVEL)
	_IRQL_requires_min_(PASSIVE_LEVEL)
	_IRQL_requires_same_

	template <typename F>
	void dispatch_on_all_cores(F&& callback, bool sequential = false)
	{
		dispatch_on_all_cores([](void* data)
		{
			(*static_cast<F*>(data))();
		}, &callback, sequential);
	}
}
