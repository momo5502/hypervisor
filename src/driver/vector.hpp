#pragma once
#include "allocator.hpp"
#include "exception.hpp"
#include "finally.hpp"

namespace utils
{
	template <typename T, typename Allocator = NonPagedAllocator>
		requires IsAllocator<Allocator>
	class vector
	{
	public:
		using type = T;

		vector() = default;

		~vector()
		{
			this->clear();
		}

		vector(const vector& obj)
			: vector()
		{
			this->operator=(obj);
		}

		vector(vector&& obj) noexcept
			: vector()
		{
			this->operator=(std::move(obj));
		}

		vector& operator=(const vector& obj)
		{
			if (this != &obj)
			{
				this->clear();
				this->reserve(obj.size_);

				for (const auto& i : obj)
				{
					this->push_back(i);
				}
			}

			return *this;
		}

		vector& operator=(vector&& obj) noexcept
		{
			if (this != &obj)
			{
				this->clear();

				this->storage_ = obj.storage_;
				this->capacity_ = obj.capacity_;
				this->size_ = obj.size_;

				obj.storage_ = nullptr;
				obj.capacity_ = 0;
				obj.size_ = 0;
			}

			return *this;
		}

		void reserve(size_t capacity)
		{
			if (this->capacity_ >= capacity)
			{
				return;
			}

			auto* old_mem = this->storage_;
			auto* old_data = this->data();

			this->storage_ = this->allocate_memory_for_capacity(capacity);
			this->capacity_ = capacity;

			auto _ = utils::finally([&old_mem, this]
			{
				this->free_memory(old_mem);
			});

			auto* data = this->data();
			for (size_t i = 0; i < this->size_; ++i)
			{
				new(data + i) T(std::move(old_data[i]));
				old_data[i].~T();
			}
		}

		T& push_back(T obj)
		{
			if (this->size_ + 1 > this->capacity_)
			{
				this->reserve(max(this->capacity_, 5) * 2);
			}

			auto* data = this->data() + this->size_++;
			new(data) T(std::move(obj));

			return *data;
		}

		T& operator[](const size_t index)
		{
			return this->at(index);
		}

		const T& operator[](const size_t index) const
		{
			return this->at(index);
		}

		T& at(const size_t index)
		{
			if (index >= this->size_)
			{
				throw std::runtime_error("Out of bounds access");
			}

			return this->data()[index];
		}

		const T& at(const size_t index) const
		{
			if (index >= this->size_)
			{
				throw std::runtime_error("Out of bounds access");
			}

			return this->data()[index];
		}

		void clear()
		{
			auto* data = this->data();
			for (size_t i = 0; i < this->size_; ++i)
			{
				data[i].~T();
			}

			free_memory(this->storage_);
			this->storage_ = nullptr;
			this->capacity_ = 0;
			this->size_ = 0;
		}

		[[nodiscard]] size_t capacity() const
		{
			return this->capacity_;
		}

		[[nodiscard]] size_t size() const
		{
			return this->size_;
		}

		T* data()
		{
			if (!this->storage_)
			{
				return nullptr;
			}

			return static_cast<T*>(align_pointer(this->storage_));
		}

		const T* data() const
		{
			if (!this->storage_)
			{
				return nullptr;
			}

			return static_cast<const T*>(align_pointer(this->storage_));
		}

		T* begin()
		{
			return this->data();
		}

		const T* begin() const
		{
			return this->data();
		}

		T* end()
		{
			return this->data() + this->size_;
		}

		const T* end() const
		{
			return this->data() + this->size_;
		}

		T* erase(T* iterator)
		{
			auto index = iterator - this->begin();
			if (index < 0 || static_cast<size_t>(index) > this->size_)
			{
				throw std::runtime_error("Bad iterator");
			}

			const auto data = this->data();
			for (size_t i = index + 1; i < this->size_; ++i)
			{
				data[i - 1] = std::move(data[i]);
			}

			data[this->size_--].~T();

			return iterator;
		}

	private:
		Allocator allocator_{};
		void* storage_{nullptr};
		size_t capacity_{0};
		size_t size_{0};

		void* allocate_memory_for_capacity(const size_t capacity)
		{
			constexpr auto alignment = alignof(T);
			auto* memory = this->allocator_.allocate(capacity * sizeof(T) + alignment);
			return memory;
		}

		void free_memory(void* memory)
		{
			this->allocator_.free(memory);
		}

		template <typename U>
		static U* align_pointer(U* pointer)
		{
			const auto align_bits = alignof(T) - 1;
			auto ptr = reinterpret_cast<intptr_t>(pointer);
			ptr = (ptr + align_bits) & (~align_bits);

			return reinterpret_cast<U*>(ptr);
		}
	};
}
