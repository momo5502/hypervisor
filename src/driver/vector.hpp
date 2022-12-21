#pragma once
#include "type_traits.hpp"
#include "memory.hpp"
#include "exception.hpp"

template <typename T>
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

			for (size_t i = 0; i < obj.size_; ++i)
			{
				this->push_back(obj.at(i));
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

		this->storage_ = allocate_memory_for_capacity(capacity);
		this->capacity_ = capacity;

		auto _ = finally([&old_mem]
		{
			free_memory(old_mem);
		});

		auto* data = this->data();
		for (size_t i = 0; i < this->size_; ++i)
		{
			new(data + i) T(std::move(old_data[i]));
			old_data[i]->~T();
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


	T& at(size_t index)
	{
		if (index >= this->size_)
		{
			throw std::runtime_error("Out of bounds access");
		}

		return this->data()[index];
	}

	const T& at(size_t index) const
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
			data[i]->~T();
		}

		free_memory(this->storage_);
		this->storage_ = nullptr;
		this->capacity_ = nullptr;
		this->size_ = nullptr;
	}

	size_t capacity() const
	{
		return this->capacity_;
	}

	size_t size() const
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

private:
	void* storage_{nullptr};
	size_t capacity_{0};
	size_t size_{0};

	static void* allocate_memory_for_capacity(const size_t capacity)
	{
		constexpr auto alignment = alignof(T);
		auto* memory = memory::allocate_non_paged_memory(capacity * sizeof(T) + alignment);
		return memory;
	}

	static void free_memory(void* memory)
	{
		memory::free_non_paged_memory(memory);
	}

	template <typename U>
	static U* align_pointer(U* pointer)
	{
		const auto align_bits = alignof(T) - 1;
		auto ptr = static_cast<intptr_t>(pointer);
		ptr = (ptr + align_bits) & (~align_bits);

		return static_cast<U*>(ptr);
	}
};
