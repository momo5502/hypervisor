#pragma once
#include "functional.hpp"

class sleep_callback
{
public:
	enum class type
	{
		sleep,
		wakeup,
	};

	using callback_function = std::function<void(type)>;

	sleep_callback() = default;
	sleep_callback(callback_function&& callback);
	~sleep_callback();

	sleep_callback(sleep_callback&& obj) noexcept = delete;
	sleep_callback& operator=(sleep_callback&& obj) noexcept = delete;

	sleep_callback(const sleep_callback& obj) = delete;
	sleep_callback& operator=(const sleep_callback& obj) = delete;

private:
	void* handle_{nullptr};
	callback_function callback_{};

	void dispatcher(type type) const;

	_Function_class_(CALLBACK_FUNCTION)
	static void static_callback(void* context, void* argument1, void* argument2);
};
