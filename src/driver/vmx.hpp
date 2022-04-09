#pragma once

#define _1GB                        (1 * 1024 * 1024 * 1024)
#define _2MB                        (2 * 1024 * 1024)

#define HYPERV_HYPERVISOR_PRESENT_BIT           0x80000000
#define HYPERV_CPUID_INTERFACE                  0x40000001

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

	struct launch_context
	{
		special_registers special_registers;
		CONTEXT context_frame;
		uint64_t system_directory_table_base;
		ULARGE_INTEGER msr_data[17];
		mtrr_range mtrr_data[16];
		uint64_t vmx_on_physical_address;
		uint64_t vmcs_physical_address;
		uint64_t msr_bitmap_physical_address;
		uint64_t ept_pml4_physical_address;
		ia32_vmx_procbased_ctls2_register ept_controls;
	};

	struct state
	{
		DECLSPEC_PAGE_ALIGN uint8_t stack_buffer[KERNEL_STACK_SIZE]{};
		DECLSPEC_PAGE_ALIGN uint8_t msr_bitmap[PAGE_SIZE]{};
		DECLSPEC_PAGE_ALIGN ept_pml4 epml4[EPT_PML4E_ENTRY_COUNT]{};
		DECLSPEC_PAGE_ALIGN epdpte epdpt[EPT_PDPTE_ENTRY_COUNT]{};
		DECLSPEC_PAGE_ALIGN epde_2mb epde[EPT_PDPTE_ENTRY_COUNT][EPT_PDE_ENTRY_COUNT]{};

		DECLSPEC_PAGE_ALIGN vmcs vmx_on{};
		DECLSPEC_PAGE_ALIGN vmcs vmcs{};
		DECLSPEC_PAGE_ALIGN launch_context launch_context{};
	};

	struct gdt_entry
	{
		uint64_t base;
		uint32_t limit;
		vmx_segment_access_rights access_rights;
		uint16_t selector;
	};

	struct guest_context
	{
		PCONTEXT vp_regs;
		uintptr_t guest_rip;
		uintptr_t guest_rsp;
		uintptr_t guest_e_flags;
		uint16_t exit_reason;
		bool exit_vm;
	};
}
