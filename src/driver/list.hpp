#pragma once
#include "allocator.hpp"
#include "exception.hpp"
#include "finally.hpp"

namespace utils
{
	template <typename T, typename ObjectAllocator = NonPagedAllocator, typename ListAllocator = NonPagedAllocator>
		requires IsAllocator<ObjectAllocator> && IsAllocator<ListAllocator>
	class list
	{
		struct ListEntry
		{
			T* entry{nullptr};
			ListEntry* next{nullptr};

			void* this_base{nullptr};
			void* entry_base{nullptr};
		};

	public:
		using type = T;

		class iterator
		{
			friend list;

		public:
			iterator(ListEntry* entry = nullptr)
				: entry_(entry)
			{
			}

			T* operator*() const
			{
				return this->entry_->entry;
			}

			T* operator->() const
			{
				return this->entry_->entry;
			}

			bool operator==(const iterator& i) const
			{
				return this->entry_ == i.entry_;
			}

			iterator operator++() const
			{
				this->entry_ = this->entry_->next;
				return *this;
			}

		private:
			ListEntry* entry_{nullptr};

			ListEntry* get_entry() const
			{
				return entry_;
			}
		};

		class const_iterator
		{
			friend list;

		public:
			const_iterator(ListEntry* entry = nullptr)
				: entry_(entry)
			{
			}

			const T* operator*() const
			{
				return this->entry_->entry;
			}

			const T* operator->() const
			{
				return this->entry_->entry;
			}

			bool operator==(const const_iterator& i) const
			{
				return this->entry_ == i.entry_;
			}

			const_iterator operator++() const
			{
				this->entry_ = this->entry_->next;
				return *this;
			}

		private:
			ListEntry* entry_{nullptr};

			ListEntry* get_entry() const
			{
				return entry_;
			}
		};

		list() = default;

		~list()
		{
			this->clear();
		}

		list(const list& obj)
			: list()
		{
			this->operator=(obj);
		}

		list(list&& obj) noexcept
			: list()
		{
			this->operator=(std::move(obj));
		}

		list& operator=(const list& obj)
		{
			if (this != &obj)
			{
				this->clear();

				for (const auto& i : obj)
				{
					this->push_back(i);
				}
			}

			return *this;
		}

		list& operator=(list&& obj) noexcept
		{
			if (this != &obj)
			{
				this->clear();

				this->entries_ = obj.entries_;
				obj.entries_ = nullptr;
			}

			return *this;
		}

		T& push_back(T obj)
		{
			auto** inseration_point = &this->entries_;
			while (*inseration_point)
			{
				inseration_point = &(*inseration_point)->next;
			}

			auto* list_base = this->list_allocator_.allocate(sizeof(ListEntry) + alignof(ListEntry));
			auto* entry_base = this->object_allocator_.allocate(sizeof(T) + alignof(T));

			auto* entry = align_pointer<T>(entry_base);
			auto* list_entry = align_pointer<ListEntry>(list_base);

			list_entry->this_base = list_base;
			list_entry->entry_base = entry_base;
			list_entry->next = nullptr;
			list_entry->entry = entry;

			*inseration_point = list_entry;

			new(entry) T(std::move(obj));

			return *entry;
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
			size_t i = 0;
			for (auto& obj : *this)
			{
				if (++i == index)
				{
					return obj;
				}
			}

			throw std::runtime_error("Out of bounds access");
		}

		const T& at(const size_t index) const
		{
			size_t i = 0;
			for (const auto& obj : *this)
			{
				if (++i == index)
				{
					return obj;
				}
			}

			throw std::runtime_error("Out of bounds access");
		}

		void clear()
		{
			while (this->entries_)
			{
				this->erase(this->begin());
			}
		}

		[[nodiscard]] size_t size() const
		{
			size_t i = 0;
			for (const auto& obj : *this)
			{
				++i;
			}

			return i;
		}

		iterator begin()
		{
			return {this->entries_};
		}

		const_iterator begin() const
		{
			return {this->entries_};
		}

		iterator end()
		{
			return {};
		}

		const_iterator end() const
		{
			return {};
		}

		iterator erase(iterator iterator)
		{
			auto* list_entry = iterator.get_entry();
			auto** inseration_point = &this->entries_;
			while (*inseration_point && list_entry)
			{
				if (*inseration_point != list_entry)
				{
					inseration_point = &(*inseration_point)->next;
					continue;
				}

				*inseration_point = list_entry->next;

				list_entry->entry->~T();
				this->object_allocator_.free(list_entry->entry_base);
				this->list_allocator_.free(list_entry->this_base);

				return iterator(*inseration_point);
			}

			throw std::runtime_error("Bad iterator");
		}

	private:
		friend iterator;

		ObjectAllocator object_allocator_{};
		ListAllocator list_allocator_{};
		ListEntry* entries_{nullptr};

		template <typename U, typename V>
		static U* align_pointer(V* pointer)
		{
			const auto align_bits = alignof(U) - 1;
			auto ptr = reinterpret_cast<intptr_t>(pointer);
			ptr = (ptr + align_bits) & (~align_bits);

			return reinterpret_cast<U*>(ptr);
		}
	};
}
