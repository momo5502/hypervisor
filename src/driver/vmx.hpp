#pragma once

namespace vmx
{
	struct vmcs
	{
		uint32_t revision_id;
		uint32_t abort_indicator;
		uint8_t data[PAGE_SIZE - 8];
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
		segment_descriptor_register_64 idtr;
		segment_descriptor_register_64 gdtr;
	};

	struct mtrr_range
	{
		uint32_t enabled;
		uint32_t type;
		uint64_t physical_address_min;
		uint64_t physical_address_max;
	};

#define DECLSPEC_PAGE_ALIGN DECLSPEC_ALIGN(PAGE_SIZE)

	struct vm_state
	{
		union
		{
			DECLSPEC_PAGE_ALIGN uint8_t stack_buffer[KERNEL_STACK_SIZE]{};

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

		DECLSPEC_PAGE_ALIGN uint8_t msr_bitmap[PAGE_SIZE]{};
		DECLSPEC_PAGE_ALIGN ept_pml4 epml4[EPT_PML4E_ENTRY_COUNT]{};
		DECLSPEC_PAGE_ALIGN epdpte epdpt[EPT_PDPTE_ENTRY_COUNT]{};
		DECLSPEC_PAGE_ALIGN epde_2mb epde[EPT_PDPTE_ENTRY_COUNT][EPT_PDE_ENTRY_COUNT]{};

		DECLSPEC_PAGE_ALIGN vmcs vmx_on{};
		DECLSPEC_PAGE_ALIGN vmcs vmcs{};
	};
}


typedef struct _VMX_GDTENTRY64
{
	UINT64 Base;
	UINT32 Limit;

	union
	{
		struct
		{
			UINT8 Flags1;
			UINT8 Flags2;
			UINT8 Flags3;
			UINT8 Flags4;
		} Bytes;

		struct
		{
			UINT16 SegmentType : 4;
			UINT16 DescriptorType : 1;
			UINT16 Dpl : 2;
			UINT16 Present : 1;

			UINT16 Reserved : 4;
			UINT16 System : 1;
			UINT16 LongMode : 1;
			UINT16 DefaultBig : 1;
			UINT16 Granularity : 1;

			UINT16 Unusable : 1;
			UINT16 Reserved2 : 15;
		} Bits;

		UINT32 AccessRights;
	};

	UINT16 Selector;
} VMX_GDTENTRY64, *PVMX_GDTENTRY64;


typedef union _KGDTENTRY64
{
	struct
	{
		UINT16 LimitLow;
		UINT16 BaseLow;

		union
		{
			struct
			{
				UINT8 BaseMiddle;
				UINT8 Flags1;
				UINT8 Flags2;
				UINT8 BaseHigh;
			} Bytes;

			struct
			{
				UINT32 BaseMiddle : 8;
				UINT32 Type : 5;
				UINT32 Dpl : 2;
				UINT32 Present : 1;
				UINT32 LimitHigh : 4;
				UINT32 System : 1;
				UINT32 LongMode : 1;
				UINT32 DefaultBig : 1;
				UINT32 Granularity : 1;
				UINT32 BaseHigh : 8;
			} Bits;
		};

		UINT32 BaseUpper;
		UINT32 MustBeZero;
	};

	struct
	{
		INT64 DataLow;
		INT64 DataHigh;
	};
} KGDTENTRY64, *PKGDTENTRY64;