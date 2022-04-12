#pragma once
#include "unique_ptr.hpp"

namespace std
{
	template <typename T>
	struct function;

	template <typename Result, typename... Args>
	struct function<Result(Args ...)>
	{
	private:
		struct fn_interface
		{
			virtual ~fn_interface() = default;
			virtual Result operator()(Args ...) const = 0;
		};

		template <typename F>
		struct fn_implementation : fn_interface
		{
			fn_implementation(F&& f) : f_(std::forward<F>(f))
			{
			}

			Result operator()(Args ... args) const override
			{
				return f_(std::forward<Args>(args)...);
			}

			F f_;
		};

		std::unique_ptr<fn_interface> fn_{};

	public:
		function() = default;

		template <typename T>
		function(T&& t)
			: fn_(new fn_implementation<T>(std::forward<T>(t)))
		{
		}

		~function() = default;
		function(function<Result(Args ...)>&&) noexcept = default;
		function& operator=(function<Result(Args ...)>&&) noexcept = default;

		function(const function<Result(Args ...)>&) = delete;
		function& operator=(const function<Result(Args ...)>&) = delete;

		Result operator()(Args ... args) const
		{
			return (*this->fn_)(std::forward<Args>(args)...);
		}

		operator bool() const
		{
			return this->fn_;
		}
	};
}
