#include "thread.hpp"
#include "std_include.hpp"
#include "logging.hpp"
#include "exception.hpp"

namespace thread
{
	namespace
	{
		struct dispatch_data
		{
			void (*callback)(void*){};
			void* data{};
		};


		_Function_class_(KDEFERRED_ROUTINE)

		void NTAPI callback_dispatcher(struct _KDPC* /*Dpc*/,
		                               const PVOID param,
		                               const PVOID arg1,
		                               const PVOID arg2)
		{
			try
			{
				const auto* const data = static_cast<dispatch_data*>(param);
				data->callback(data->data);
			}
			catch (std::exception& e)
			{
				debug_log("Exception during dpc on core %d: %s\n", get_processor_index(), e.what());
			}
			catch (...)
			{
				debug_log("Unknown exception during dpc on core %d\n", get_processor_index());
			}

			KeSignalCallDpcSynchronize(arg2);
			KeSignalCallDpcDone(arg1);
		}

		void NTAPI sequential_callback_dispatcher(struct _KDPC* /*Dpc*/,
		                                          const PVOID param,
		                                          const PVOID arg1,
		                                          const PVOID arg2)
		{
			const auto cpu_count = get_processor_count();
			const auto current_cpu = get_processor_index();

			for (auto i = 0u; i < cpu_count; ++i)
			{
				if (i == current_cpu)
				{
					try
					{
						const auto* const data = static_cast<dispatch_data*>(param);
						data->callback(data->data);
					}
					catch (std::exception& e)
					{
						debug_log("Exception during dpc on core %d: %s\n", get_processor_index(), e.what());
					}
					catch (...)
					{
						debug_log("Unknown exception during dpc on core %d\n", get_processor_index());
					}
				}

				KeSignalCallDpcSynchronize(arg2);
			}

			KeSignalCallDpcDone(arg1);
		}
	}

	uint32_t get_processor_count()
	{
		return static_cast<uint32_t>(KeQueryActiveProcessorCountEx(0));
	}

	uint32_t get_processor_index()
	{
		return static_cast<uint32_t>(KeGetCurrentProcessorNumberEx(nullptr));
	}

	bool sleep(const uint32_t milliseconds)
	{
		LARGE_INTEGER interval{};
		interval.QuadPart = -(10000ll * milliseconds);

		return STATUS_SUCCESS == KeDelayExecutionThread(KernelMode, FALSE, &interval);
	}

	void dispatch_on_all_cores(void (*callback)(void*), void* data, const bool sequential)
	{
		dispatch_data callback_data{};
		callback_data.callback = callback;
		callback_data.data = data;

		KeGenericCallDpc(sequential ? sequential_callback_dispatcher : callback_dispatcher, &callback_data);
	}
}
