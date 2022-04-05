#pragma once

#ifdef ASSERT
#undef ASSERT
#endif

#define ASSERT( exp ) if(!(exp)) throw std::runtime_error("Assertion failed: " STRINGIFY(exp))

#ifdef NT_ASSERT
#undef NT_ASSERT
#endif

#define NT_ASSERT( exp ) ASSERT( exp )

namespace std
{
	class exception
	{
	public:
		exception& operator=(const exception& obj) noexcept = default;
		exception& operator=(exception&& obj) noexcept = default;

		virtual ~exception() = default;
		virtual const char* what() const noexcept = 0;
	};

	class runtime_error : public exception
	{
	public:
		runtime_error(const char* message)
			: message_(message)
		{
		}

		runtime_error(const runtime_error& obj) noexcept = default;
		runtime_error& operator=(const runtime_error& obj) noexcept = default;

		runtime_error(runtime_error&& obj) noexcept = default;
		runtime_error& operator=(runtime_error&& obj) noexcept = default;

		const char* what() const noexcept override
		{
			return message_;
		}

	private:
		const char* message_{};
	};
}
