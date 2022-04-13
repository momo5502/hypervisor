#pragma once

#define DECLSPEC_PAGE_ALIGN DECLSPEC_ALIGN(PAGE_SIZE)

namespace vmx
{
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

		void install_hook(void* virtual_address, void* data, size_t length);
		void install_hook(uint64_t physical_address, void* data, size_t length);

		ept_pml4* get_pml4();
		const ept_pml4* get_pml4() const;

	private:
		DECLSPEC_PAGE_ALIGN ept_pml4 epml4[EPT_PML4E_ENTRY_COUNT]{};
		DECLSPEC_PAGE_ALIGN epdpte epdpt[EPT_PDPTE_ENTRY_COUNT]{};
		DECLSPEC_PAGE_ALIGN epde_2mb epde[EPT_PDPTE_ENTRY_COUNT][EPT_PDE_ENTRY_COUNT]{};
	};
}
