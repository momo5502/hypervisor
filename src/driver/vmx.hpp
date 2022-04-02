#pragma once

#include <ia32.hpp>

#define PML4E_ENTRY_COUNT   512 // EPT_PML4E_ENTRY_COUNT
#define PDPTE_ENTRY_COUNT   512 // EPT_PDPTE_ENTRY_COUNT
#define PDE_ENTRY_COUNT     512 // EPT_PDE_ENTRY_COUNT

namespace vmx
{
	struct vmcs
	{
		uint32_t revision_id;
		uint32_t abort_indicator;
		uint8_t data[PAGE_SIZE - 8];
	};

	struct epml4e
	{
		union
		{
			struct
			{
				uint64_t read : 1;
				uint64_t write : 1;
				uint64_t execute : 1;
				uint64_t reserved : 5;
				uint64_t accessed : 1;
				uint64_t software_use : 1;
				uint64_t user_mode_execute : 1;
				uint64_t software_use2 : 1;
				uint64_t page_frame_number : 36;
				uint64_t reserved_high : 4;
				uint64_t software_use_high : 12;
			};

			uint64_t full;
		};
	};

	struct pdpte
	{
		union
		{
			struct
			{
				uint64_t read : 1;
				uint64_t write : 1;
				uint64_t execute : 1;
				uint64_t reserved : 5;
				uint64_t accessed : 1;
				uint64_t software_use : 1;
				uint64_t user_mode_execute : 1;
				uint64_t software_use2 : 1;
				uint64_t page_frame_number : 36;
				uint64_t reserved_high : 4;
				uint64_t software_use_high : 12;
			};

			uint64_t full;
		};
	};

	struct kdescriptor
	{
		uint16_t pad[3];
		uint16_t limit;
		void* base;
	};

	struct special_registers
	{
		uint64_t cr0;
		uint64_t cr3;
		uint64_t cr4;
		uint64_t msr_gs_base;
		uint16_t tr;
		uint16_t ldtr;
		uint64_t debug_control;
		uint64_t kernel_dr7;
		kdescriptor idtr;
		kdescriptor gdtr;
	};

	struct mtrr_range
	{
		uint32_t enabled;
		uint32_t type;
		uint64_t physical_address_min;
		uint64_t physical_address_max;
	};

	struct vm_state
	{
		union
		{
			DECLSPEC_ALIGN(PAGE_SIZE) uint8_t stack_buffer[KERNEL_STACK_SIZE]{};

			struct
			{
				struct special_registers special_registers;
				CONTEXT context_frame;
				uint64_t system_directory_table_base;
				LARGE_INTEGER msr_data[17];
				mtrr_range mtrr_data[16];
				uint64_t vmx_on_physical_address;
				uint64_t vmcs_physical_address;
				uint64_t msr_bitmap_physical_address;
				uint64_t ept_pml4_physical_address;
				ia32_vmx_procbased_ctls2_register ept_controls;
			};
		};

		DECLSPEC_ALIGN(PAGE_SIZE) uint8_t msr_bitmap[PAGE_SIZE]{};
		DECLSPEC_ALIGN(PAGE_SIZE) epml4e epml4[PML4E_ENTRY_COUNT]{};
		DECLSPEC_ALIGN(PAGE_SIZE) pdpte epdpt[PDPTE_ENTRY_COUNT]{};
		DECLSPEC_ALIGN(PAGE_SIZE) epde_2mb epde[PDPTE_ENTRY_COUNT][PDE_ENTRY_COUNT]{};

		DECLSPEC_ALIGN(PAGE_SIZE) vmcs vmx_on{};
		DECLSPEC_ALIGN(PAGE_SIZE) vmcs vmcs{};
	};
}
