#pragma once
#include "allocator.hpp"
#include "exception.hpp"
#include "finally.hpp"

namespace utils
{
	template <typename T, typename ObjectAllocator = NonPagedAllocator, typename ListAllocator = NonPagedAllocator>
		requires is_allocator<ObjectAllocator> && is_allocator<ListAllocator>
	class list
	{
		struct list_entry
		{
			T* entry{nullptr};
			list_entry* next{nullptr};

			void* this_base{nullptr};
			void* entry_base{nullptr};
		};

	public:
		using type = T;

		class iterator
		{
			friend list;

		public:
			iterator(list_entry* entry = nullptr)
				: entry_(entry)
			{
			}

			T& operator*() const
			{
				return *this->entry_->entry;
			}

			T* operator->() const
			{
				return this->entry_->entry;
			}

			bool operator==(const iterator& i) const
			{
				return this->entry_ == i.entry_;
			}

			iterator operator++()
			{
				this->entry_ = this->entry_->next;
				return *this;
			}

			iterator operator+(const size_t num) const
			{
				auto entry = this->entry_;

				for (size_t i = 0; i < num; ++i)
				{
					if (!entry)
					{
						return {};
					}

					entry = entry->next;
				}


				return {entry};
			}

		private:
			list_entry* entry_{nullptr};

			list_entry* get_entry() const
			{
				return entry_;
			}
		};

		class const_iterator
		{
			friend list;

		public:
			const_iterator(list_entry* entry = nullptr)
				: entry_(entry)
			{
			}

			const T& operator*() const
			{
				return *this->entry_->entry;
			}

			const T* operator->() const
			{
				return this->entry_->entry;
			}

			bool operator==(const const_iterator& i) const
			{
				return this->entry_ == i.entry_;
			}

			const_iterator operator++()
			{
				this->entry_ = this->entry_->next;
				return *this;
			}

		private:
			list_entry* entry_{nullptr};

			list_entry* get_entry() const
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

		T& push_back(const T& obj)
		{
			auto& entry = this->add_uninitialized_entry_back();
			new(&entry) T(obj);
			return entry;
		}

		T& push_back(T&& obj)
		{
			auto& entry = this->add_uninitialized_entry_back();
			new(&entry) T(std::move(obj));
			return entry;
		}

		template <typename... Args>
		T& emplace_back(Args&&... args)
		{
			auto& entry = this->add_uninitialized_entry_back();
			new(&entry) T(std::forward<Args>(args)...);
			return entry;
		}

		T& push_front(const T& obj)
		{
			auto& entry = this->add_uninitialized_entry_front();
			new(&entry) T(obj);
			return entry;
		}

		T& push_front(T&& obj)
		{
			auto& entry = this->add_uninitialized_entry_front();
			new(&entry) T(std::move(obj));
			return entry;
		}

		template <typename... Args>
		T& emplace_front(Args&&... args)
		{
			auto& entry = this->add_uninitialized_entry_front();
			new(&entry) T(std::forward<Args>(args)...);
			return entry;
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
			while (!this->empty())
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

		void erase(T& entry)
		{
			auto** insertion_point = &this->entries_;
			while (*insertion_point)
			{
				if ((*insertion_point)->entry != &entry)
				{
					insertion_point = &(*insertion_point)->next;
					continue;
				}

				auto* list_entry = *insertion_point;
				*insertion_point = list_entry->next;

				list_entry->entry->~T();
				this->object_allocator_.free(list_entry->entry_base);
				this->list_allocator_.free(list_entry->this_base);

				return;
			}

			throw std::runtime_error("Bad entry");
		}

		iterator erase(iterator iterator)
		{
			auto* list_entry = iterator.get_entry();
			auto** insertion_point = &this->entries_;
			while (*insertion_point && list_entry)
			{
				if (*insertion_point != list_entry)
				{
					insertion_point = &(*insertion_point)->next;
					continue;
				}

				*insertion_point = list_entry->next;

				list_entry->entry->~T();
				this->object_allocator_.free(list_entry->entry_base);
				this->list_allocator_.free(list_entry->this_base);

				return {*insertion_point};
			}

			throw std::runtime_error("Bad iterator");
		}

		bool empty() const
		{
			return this->entries_ == nullptr;
		}

	private:
		friend iterator;

		ObjectAllocator object_allocator_{};
		ListAllocator list_allocator_{};
		list_entry* entries_{nullptr};

		template <typename U, typename V>
		static U* align_pointer(V* pointer)
		{
			const auto align_bits = alignof(U) - 1;
			auto ptr = reinterpret_cast<intptr_t>(pointer);
			ptr = (ptr + align_bits) & (~align_bits);

			return reinterpret_cast<U*>(ptr);
		}

		void allocate_entry(void*& list_base, void*& entry_base)
		{
			list_base = nullptr;
			entry_base = nullptr;

			auto destructor = utils::finally([&]
			{
				if (list_base)
				{
					this->list_allocator_.free(list_base);
				}

				if (entry_base)
				{
					this->object_allocator_.free(entry_base);
				}
			});

			list_base = this->list_allocator_.allocate(sizeof(list_entry) + alignof(list_entry));
			if (!list_base)
			{
				throw std::runtime_error("Memory allocation failed");
			}

			entry_base = this->object_allocator_.allocate(sizeof(T) + alignof(T));
			if (!entry_base)
			{
				throw std::runtime_error("Memory allocation failed");
			}

			destructor.cancel();
		}

		list_entry& create_uninitialized_list_entry()
		{
			void* list_base = {};
			void* entry_base = {};
			this->allocate_entry(list_base, entry_base);

			auto* obj = align_pointer<T>(entry_base);
			auto* entry = align_pointer<list_entry>(list_base);

			entry->this_base = list_base;
			entry->entry_base = entry_base;
			entry->next = nullptr;
			entry->entry = obj;

			return *entry;
		}

		T& add_uninitialized_entry_back()
		{
			auto** insertion_point = &this->entries_;
			while (*insertion_point)
			{
				insertion_point = &(*insertion_point)->next;
			}

			auto& entry = this->create_uninitialized_list_entry();
			*insertion_point = &entry;

			return *entry.entry;
		}

		T& add_uninitialized_entry_front()
		{
			auto& entry = this->create_uninitialized_list_entry();
			entry.next = this->entries_;
			this->entries_ = &entry;

			return *entry.entry;
		}
	};
}
