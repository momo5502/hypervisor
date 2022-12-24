#pragma once

#define DECLSPEC_PAGE_ALIGN DECLSPEC_ALIGN(PAGE_SIZE)
#include "list.hpp"

namespace vmx
{
	using pml4 = ept_pml4;
	using pml3 = epdpte;
	using pml2 = epde_2mb;
	using pml2_ptr = epde;
	using pml1 = epte;

	struct ept_split
	{
		DECLSPEC_PAGE_ALIGN pml1 pml1[EPT_PTE_ENTRY_COUNT]{};

		union
		{
			pml2 entry{};
			pml2_ptr pointer;
		};
	};

	struct ept_code_watch_point
	{
		uint64_t physical_base_address{};
		pml1* target_page{};
	};

	struct ept_hook
	{
		ept_hook(uint64_t physical_base);
		~ept_hook();

		DECLSPEC_PAGE_ALIGN uint8_t fake_page[PAGE_SIZE]{};
		DECLSPEC_PAGE_ALIGN uint8_t diff_page[PAGE_SIZE]{};

		uint64_t physical_base_address{};
		void* mapped_virtual_address{};

		pml1* target_page{};
		pml1 original_entry{};
		pml1 execute_entry{};
		pml1 readwrite_entry{};
	};

	struct ept_translation_hint
	{
		DECLSPEC_PAGE_ALIGN uint8_t page[PAGE_SIZE]{};

		uint64_t physical_base_address{};
		const void* virtual_base_address{};
	};

	struct guest_context;

	class ept
	{
	public:
		ept();
		~ept();

		ept(ept&&) = delete;
		ept(const ept&) = delete;
		ept& operator=(ept&&) = delete;
		ept& operator=(const ept&) = delete;

		void initialize();

		void install_code_watch_point(uint64_t physical_page);

		void install_hook(const void* destination, const void* source, size_t length,
		                  const utils::list<ept_translation_hint>& hints = {});
		void disable_all_hooks() const;

		void handle_violation(guest_context& guest_context);
		void handle_misconfiguration(guest_context& guest_context) const;

		ept_pointer get_ept_pointer() const;
		void invalidate() const;

		static utils::list<ept_translation_hint> generate_translation_hints(const void* destination, size_t length);

		uint64_t* get_access_records(size_t* count);

	private:
		DECLSPEC_PAGE_ALIGN pml4 epml4[EPT_PML4E_ENTRY_COUNT];
		DECLSPEC_PAGE_ALIGN pml3 epdpt[EPT_PDPTE_ENTRY_COUNT];
		DECLSPEC_PAGE_ALIGN pml2 epde[EPT_PDPTE_ENTRY_COUNT][EPT_PDE_ENTRY_COUNT];

		uint64_t access_records[1024];

		utils::list<ept_split, utils::AlignedAllocator> ept_splits{};
		utils::list<ept_hook, utils::AlignedAllocator> ept_hooks{};
		utils::list<ept_code_watch_point> ept_code_watch_points{};

		pml2* get_pml2_entry(uint64_t physical_address);
		pml1* get_pml1_entry(uint64_t physical_address);
		pml1* find_pml1_table(uint64_t physical_address);

		ept_split& allocate_ept_split();
		ept_hook& allocate_ept_hook(uint64_t physical_address);
		ept_hook* find_ept_hook(uint64_t physical_address);

		ept_code_watch_point& allocate_ept_code_watch_point();
		ept_code_watch_point* find_ept_code_watch_point(uint64_t physical_address);

		ept_hook* get_or_create_ept_hook(void* destination, const ept_translation_hint* translation_hint = nullptr);

		void split_large_page(uint64_t physical_address);

		void install_page_hook(void* destination, const void* source, size_t length,
		                       const ept_translation_hint* translation_hint = nullptr);

		void record_access(uint64_t rip);
	};
}
