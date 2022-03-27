#pragma once

#define PML4E_ENTRY_COUNT   512
#define PDPTE_ENTRY_COUNT   512
#define PDE_ENTRY_COUNT     512

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

	struct large_pde
	{
		union
		{
			struct
			{
				uint64_t read : 1;
				uint64_t write : 1;
				uint64_t execute : 1;
				uint64_t type : 3;
				uint64_t ignore_pat : 1;
				uint64_t large : 1;
				uint64_t accessed : 1;
				uint64_t dirty : 1;
				uint64_t user_mode_execute : 1;
				uint64_t software_use : 1;
				uint64_t reserved : 9;
				uint64_t page_frame_number : 27;
				uint64_t reserved_high : 4;
				uint64_t software_use_high : 11;
				uint64_t suppress_vme : 1;
			};

			uint64_t full;
		};
	};

	struct vm_state
	{
		DECLSPEC_ALIGN(PAGE_SIZE) uint8_t stack_buffer[KERNEL_STACK_SIZE]{};

		DECLSPEC_ALIGN(PAGE_SIZE) uint8_t msr_bitmap[PAGE_SIZE]{};
		DECLSPEC_ALIGN(PAGE_SIZE) epml4e epml4[PML4E_ENTRY_COUNT]{};
		DECLSPEC_ALIGN(PAGE_SIZE) pdpte epdpt[PDPTE_ENTRY_COUNT]{};
		DECLSPEC_ALIGN(PAGE_SIZE) large_pde epde[PDPTE_ENTRY_COUNT][PDE_ENTRY_COUNT]{};

		DECLSPEC_ALIGN(PAGE_SIZE) vmcs vmx_on{};
		DECLSPEC_ALIGN(PAGE_SIZE) vmcs vmcs{};
	};
}
