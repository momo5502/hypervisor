#pragma once
#include "type_traits.hpp"

namespace std
{
	template <typename T>
	class unique_ptr
	{
	public:
		using value_type = typename remove_extent<T>::type;

		unique_ptr() = default;

		unique_ptr(value_type* pointer)
			: pointer_(pointer)
		{
		}

		~unique_ptr()
		{
			if (this->pointer_)
			{
				delete_pointer();
				this->pointer_ = nullptr;
			}
		}

		unique_ptr(unique_ptr<T>&& obj) noexcept
			: unique_ptr()
		{
			this->operator=(std::move(obj));
		}

		unique_ptr& operator=(unique_ptr<T>&& obj) noexcept
		{
			if (this != &obj)
			{
				this->~unique_ptr();
				this->pointer_ = obj.pointer_;
				obj.pointer_ = nullptr;
			}

			return *this;
		}

		unique_ptr(const unique_ptr<T>& obj) = delete;
		unique_ptr& operator=(const unique_ptr<T>& obj) = delete;

		value_type* get()
		{
			return this->pointer_;
		}

		value_type* operator->()
		{
			return this->pointer_;
		}

		const value_type* operator->() const
		{
			return this->pointer_;
		}

		value_type& operator*()
		{
			return *this->pointer_;
		}

		const value_type& operator*() const
		{
			return *this->pointer_;
		}

		operator bool() const
		{
			return this->pointer_;
		}

	private:
		static constexpr auto is_array_type = is_array<T>::value;
		value_type* pointer_{nullptr};

		void delete_pointer() const
		{
			if (is_array_type)
			{
				delete[] this->pointer_;
			}
			else
			{
				delete this->pointer_;
			}
		}
	};
}
