#pragma once
#include "type_traits.hpp"

namespace std
{
	template <typename T>
	class unique_ptr
	{
	public:
		unique_ptr() = default;

		unique_ptr(T* pointer)
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

		T* operator->()
		{
			return this->pointer_;
		}

		const T* operator->() const
		{
			return this->pointer_;
		}

		T& operator*()
		{
			return *this->pointer_;
		}

		const T& operator*() const
		{
			return *this->pointer_;
		}

		operator bool() const
		{
			return this->pointer_;
		}

	private:
		static constexpr auto is_array_type = is_array<T>::value;
		T* pointer_{nullptr};

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
