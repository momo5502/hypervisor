#include "std_include.hpp"
#include "process_callback.hpp"
#include "list.hpp"
#include "logging.hpp"

namespace process_callback
{
	namespace
	{
		utils::list<callback_function>& get_callback_list()
		{
			static utils::list<callback_function> list{};
			return list;
		}

		void process_notification_callback(const HANDLE parent_id, const HANDLE process_id, const BOOLEAN create)
		{
			const auto& list = get_callback_list();
			for (const auto& callback : list)
			{
				callback(parent_id, process_id, create == FALSE ? type::destroy : type::create);
			}
		}

		class process_notifier
		{
		public:
			process_notifier()
				: added_(PsSetCreateProcessNotifyRoutine(process_notification_callback, FALSE) == STATUS_SUCCESS)
			{
				get_callback_list();

				if (!added_)
				{
					debug_log("Failed to register process notification callback\n");
				}
			}

			~process_notifier()
			{
				if (this->added_)
				{
					PsSetCreateProcessNotifyRoutine(process_notification_callback, TRUE);
				}
			}

		private:
			bool added_{};
		};
	}

	void* add(callback_function callback)
	{
		static process_notifier _;
		return &get_callback_list().push_back(std::move(callback));
	}

	void remove(void* handle)
	{
		if (handle)
		{
			get_callback_list().erase(*static_cast<callback_function*>(handle));
		}
	}
}
