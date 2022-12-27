#pragma once
#include "functional.hpp"

namespace process_callback
{
	enum class type
	{
		create,
		destroy,
	};

	using callback = void(HANDLE parent_id, HANDLE process_id, type type);
	using callback_function = std::function<callback>;

	void* add(callback_function callback);
	void remove(void* handle);

	class scoped_process_callback
	{
	public:
		scoped_process_callback() = default;

		scoped_process_callback(callback_function function)
			: handle_(add(std::move(function)))
		{
		}

		~scoped_process_callback()
		{
			remove(this->handle_);
		}

		scoped_process_callback(scoped_process_callback&& obj) noexcept = delete;
		scoped_process_callback& operator=(scoped_process_callback&& obj) noexcept = delete;

		scoped_process_callback(const scoped_process_callback& obj) = delete;
		scoped_process_callback& operator=(const scoped_process_callback& obj) = delete;

	private:
		void* handle_{};
	};
}
