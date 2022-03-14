#pragma once

using uint32_t = int;

namespace thread
{
	uint32_t get_processor_count();
	uint32_t get_processor_index();

	bool sleep(uint32_t milliseconds);

	void dispatch_on_all_cores(void(*callback)(void*), void* data);

	template<typename F>
	void dispatch_on_all_cores(F&& callback)
	{
		dispatch_on_all_cores([](void* data)
		{
			(*reinterpret_cast<F*>(data))();
		}, &callback);
	}
}
