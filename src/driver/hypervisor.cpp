#include "std_include.hpp"
#include "hypervisor.hpp"

#include "exception.hpp"
#include "logging.hpp"
#include "finally.hpp"
#include "memory.hpp"
#include "thread.hpp"
#include "assembly.hpp"
#include "string.hpp"

namespace
{
	hypervisor* instance{nullptr};

	bool is_vmx_supported()
	{
		cpuid_eax_01 data{};
		__cpuid(reinterpret_cast<int*>(&data), CPUID_VERSION_INFORMATION);
		return data.cpuid_feature_information_ecx.virtual_machine_extensions;
	}

	bool is_vmx_available()
	{
		ia32_feature_control_register feature_control{};
		feature_control.flags = __readmsr(IA32_FEATURE_CONTROL);
		return feature_control.lock_bit && feature_control.enable_vmx_outside_smx;
	}

	bool is_virtualization_supported()
	{
		return is_vmx_supported() && is_vmx_available();
	}

	bool is_hypervisor_present()
	{
		cpuid_eax_01 data{};
		__cpuid(reinterpret_cast<int*>(&data), CPUID_VERSION_INFORMATION);
		if ((data.cpuid_feature_information_ecx.flags & HYPERV_HYPERVISOR_PRESENT_BIT) == 0)
		{
			return false;
		}

		int32_t cpuid_data[4] = {0};
		__cpuid(cpuid_data, HYPERV_CPUID_INTERFACE);
		return cpuid_data[0] == 'momo';
	}

	void cpature_special_registers(vmx::special_registers& special_registers)
	{
		special_registers.cr0 = __readcr0();
		special_registers.cr3 = __readcr3();
		special_registers.cr4 = __readcr4();
		special_registers.debug_control = __readmsr(IA32_DEBUGCTL);
		special_registers.msr_gs_base = __readmsr(IA32_GS_BASE);
		special_registers.kernel_dr7 = __readdr(7);
		_sgdt(&special_registers.gdtr);
		__sidt(&special_registers.idtr);
		_str(&special_registers.tr);
		_sldt(&special_registers.ldtr);
	}

	void capture_cpu_context(vmx::launch_context& launch_context)
	{
		cpature_special_registers(launch_context.special_registers);
		RtlCaptureContext(&launch_context.context_frame);
	}


	void restore_descriptor_tables(vmx::launch_context& launch_context)
	{
		__lgdt(&launch_context.special_registers.gdtr);
		__lidt(&launch_context.special_registers.idtr);
	}

	vmx::state* resolve_vm_state_from_context(CONTEXT& context)
	{
		auto* context_address = reinterpret_cast<uint8_t*>(&context);
		auto* vm_state_address = context_address + sizeof(CONTEXT) - KERNEL_STACK_SIZE;
		return reinterpret_cast<vmx::state*>(vm_state_address);
	}

	uintptr_t read_vmx(const uint32_t vmcs_field_id)
	{
		size_t data{};
		__vmx_vmread(vmcs_field_id, &data);
		return data;
	}

	[[ noreturn ]] void resume_vmx()
	{
		__vmx_vmresume();
	}

	int32_t launch_vmx()
	{
		__vmx_vmlaunch();

		const auto error_code = static_cast<int32_t>(read_vmx(VMCS_VM_INSTRUCTION_ERROR));
		__vmx_off();

		return error_code;
	}

	extern "C" [[ noreturn ]] void vm_launch_handler(CONTEXT* context)
	{
		auto* vm_state = resolve_vm_state_from_context(*context);

		vm_state->launch_context.context_frame.EFlags |= EFLAGS_ALIGNMENT_CHECK_FLAG_FLAG;
		restore_context(&vm_state->launch_context.context_frame);
	}
}

hypervisor::hypervisor()
{
	if (instance != nullptr)
	{
		throw std::runtime_error("Hypervisor already instantiated");
	}

	auto destructor = utils::finally([this]()
	{
		this->free_vm_states();
		instance = nullptr;
	});

	instance = this;

	if (!is_virtualization_supported())
	{
		throw std::runtime_error("VMX not supported on this machine");
	}

	debug_log("VMX supported!\n");
	this->allocate_vm_states();
	this->enable();
	destructor.cancel();
}

hypervisor::~hypervisor()
{
	this->disable();
	this->free_vm_states();
	instance = nullptr;
}

void hypervisor::disable()
{
	thread::dispatch_on_all_cores([this]()
	{
		this->disable_core();
	});

	debug_log("Hypervisor disabled on all cores\n");
}

bool hypervisor::is_enabled() const
{
	return is_hypervisor_present();
}

void hypervisor::enable()
{
	const auto cr3 = __readcr3();

	volatile long failures = 0;
	thread::dispatch_on_all_cores([&]()
	{
		if (!this->try_enable_core(cr3))
		{
			InterlockedIncrement(&failures);
		}
	});

	if (failures)
	{
		this->disable();
		throw std::runtime_error("Hypervisor initialization failed");
	}

	debug_log("Hypervisor enabled on %d cores\n", this->vm_state_count_);
}

bool hypervisor::try_enable_core(const uint64_t system_directory_table_base)
{
	try
	{
		this->enable_core(system_directory_table_base);
		return true;
	}
	catch (std::exception& e)
	{
		debug_log("Failed to enable hypervisor on core %d: %s\n", thread::get_processor_index(), e.what());
		return false;
	}
	catch (...)
	{
		debug_log("Failed to enable hypervisor on core %d.\n", thread::get_processor_index());
		return false;
	}
}

#define MTRR_PAGE_SIZE          4096
#define MTRR_PAGE_MASK          (~(MTRR_PAGE_SIZE-1))

VOID ShvVmxMtrrInitialize(vmx::state* VpData)
{
	ia32_mtrr_capabilities_register mtrrCapabilities;
	ia32_mtrr_physbase_register mtrrBase;
	ia32_mtrr_physmask_register mtrrMask;

	auto* launch_context = &VpData->launch_context;

	//
	// Read the capabilities mask
	//
	mtrrCapabilities.flags = __readmsr(IA32_MTRR_CAPABILITIES);

	//
	// Iterate over each variable MTRR
	//
	for (auto i = 0u; i < mtrrCapabilities.variable_range_count; i++)
	{
		//
		// Capture the value
		//
		mtrrBase.flags = __readmsr(IA32_MTRR_PHYSBASE0 + i * 2);
		mtrrMask.flags = __readmsr(IA32_MTRR_PHYSMASK0 + i * 2);

		//
		// Check if the MTRR is enabled
		//
		launch_context->mtrr_data[i].type = (UINT32)mtrrBase.type;
		launch_context->mtrr_data[i].enabled = (UINT32)mtrrMask.valid;
		if (launch_context->mtrr_data[i].enabled != FALSE)
		{
			//
			// Set the base
			//
			launch_context->mtrr_data[i].physical_address_min = mtrrBase.page_frame_number *
				MTRR_PAGE_SIZE;

			//
			// Compute the length
			//
			unsigned long bit;
			_BitScanForward64(&bit, mtrrMask.page_frame_number * MTRR_PAGE_SIZE);
			launch_context->mtrr_data[i].physical_address_max = launch_context->mtrr_data[i].
				physical_address_min +
				(1ULL << bit) - 1;
		}
	}
}

UINT32
ShvVmxMtrrAdjustEffectiveMemoryType(
	vmx::state* VpData,
	_In_ UINT64 LargePageAddress,
	_In_ UINT32 CandidateMemoryType
)
{
	auto* launch_context = &VpData->launch_context;

	//
	// Loop each MTRR range
	//
	for (auto i = 0u; i < sizeof(launch_context->mtrr_data) / sizeof(launch_context->mtrr_data[0]); i++)
	{
		//
		// Check if it's active
		//
		if (launch_context->mtrr_data[i].enabled != FALSE)
		{
			//
			// Check if this large page falls within the boundary. If a single
			// physical page (4KB) touches it, we need to override the entire 2MB.
			//
			if (((LargePageAddress + (_2MB - 1)) >= launch_context->mtrr_data[i].physical_address_min) &&
				(LargePageAddress <= launch_context->mtrr_data[i].physical_address_max))
			{
				//
				// Override candidate type with MTRR type
				//
				CandidateMemoryType = launch_context->mtrr_data[i].type;
			}
		}
	}

	//
	// Return the correct type needed
	//
	return CandidateMemoryType;
}

void ShvVmxEptInitialize(vmx::state* VpData)
{
	//
	// Fill out the EPML4E which covers the first 512GB of RAM
	//
	VpData->epml4[0].read_access = 1;
	VpData->epml4[0].write_access = 1;
	VpData->epml4[0].execute_access = 1;
	VpData->epml4[0].page_frame_number = memory::get_physical_address(&VpData->epdpt) /
		PAGE_SIZE;

	//
	// Fill out a RWX PDPTE
	//
	epdpte temp_epdpte;
	temp_epdpte.flags = 0;
	temp_epdpte.read_access = 1;
	temp_epdpte.write_access = 1;
	temp_epdpte.execute_access = 1;

	//
	// Construct EPT identity map for every 1GB of RAM
	//
	__stosq((UINT64*)VpData->epdpt, temp_epdpte.flags, EPT_PDPTE_ENTRY_COUNT);
	for (auto i = 0; i < EPT_PDPTE_ENTRY_COUNT; i++)
	{
		//
		// Set the page frame number of the PDE table
		//
		VpData->epdpt[i].page_frame_number = memory::get_physical_address(&VpData->epde[i][0]) / PAGE_SIZE;
	}

	//
	// Fill out a RWX Large PDE
	//
	epde_2mb temp_epde;
	temp_epde.flags = 0;
	temp_epde.read_access = 1;
	temp_epde.write_access = 1;
	temp_epde.execute_access = 1;
	temp_epde.large_page = 1;

	//
	// Loop every 1GB of RAM (described by the PDPTE)
	//
	__stosq((UINT64*)VpData->epde, temp_epde.flags, EPT_PDPTE_ENTRY_COUNT * EPT_PDE_ENTRY_COUNT);
	for (auto i = 0; i < EPT_PDPTE_ENTRY_COUNT; i++)
	{
		//
		// Construct EPT identity map for every 2MB of RAM
		//
		for (auto j = 0; j < EPT_PDE_ENTRY_COUNT; j++)
		{
			VpData->epde[i][j].page_frame_number = (i * 512) + j;
			VpData->epde[i][j].memory_type = ShvVmxMtrrAdjustEffectiveMemoryType(VpData,
				VpData->epde[i][j].page_frame_number * _2MB,
				MEMORY_TYPE_WRITE_BACK);
		}
	}
}


bool
ShvVmxEnterRootModeOnVp(vmx::state* VpData)
{
	auto* launch_context = &VpData->launch_context;
	auto* Registers = &launch_context->special_registers;

	//
	// Ensure the the VMCS can fit into a single page
	//
	ia32_vmx_basic_register basic_register{};
	basic_register.flags = launch_context->msr_data[0].QuadPart;
	if (basic_register.vmcs_size_in_bytes > PAGE_SIZE)
	{
		return FALSE;
	}

	//
	// Ensure that the VMCS is supported in writeback memory
	//
	if (basic_register.memory_type != MEMORY_TYPE_WRITE_BACK)
	{
		return FALSE;
	}

	//
	// Ensure that true MSRs can be used for capabilities
	//
	if (basic_register.must_be_zero)
	{
		return FALSE;
	}

	//
	// Ensure that EPT is available with the needed features SimpleVisor uses
	//
	ia32_vmx_ept_vpid_cap_register ept_vpid_cap_register{};
	ept_vpid_cap_register.flags = launch_context->msr_data[12].QuadPart;

	if (ept_vpid_cap_register.page_walk_length_4 &&
		ept_vpid_cap_register.memory_type_write_back &&
		ept_vpid_cap_register.pde_2mb_pages)
	{
		//
		// Enable EPT if these features are supported
		//
		launch_context->ept_controls.flags = 0;
		launch_context->ept_controls.enable_ept = 1;
		launch_context->ept_controls.enable_vpid = 1;
	}

	//
	// Capture the revision ID for the VMXON and VMCS region
	//
	VpData->vmx_on.revision_id = launch_context->msr_data[0].LowPart;
	VpData->vmcs.revision_id = launch_context->msr_data[0].LowPart;

	//
	// Store the physical addresses of all per-LP structures allocated
	//
	launch_context->vmx_on_physical_address = memory::get_physical_address(&VpData->vmx_on);
	launch_context->vmcs_physical_address = memory::get_physical_address(&VpData->vmcs);
	launch_context->msr_bitmap_physical_address = memory::get_physical_address(VpData->msr_bitmap);
	launch_context->ept_pml4_physical_address = memory::get_physical_address(&VpData->epml4);

	//
	// Update CR0 with the must-be-zero and must-be-one requirements
	//
	Registers->cr0 &= launch_context->msr_data[7].LowPart;
	Registers->cr0 |= launch_context->msr_data[6].LowPart;

	//
	// Do the same for CR4
	//
	Registers->cr4 &= launch_context->msr_data[9].LowPart;
	Registers->cr4 |= launch_context->msr_data[8].LowPart;

	//
	// Update host CR0 and CR4 based on the requirements above
	//
	__writecr0(Registers->cr0);
	__writecr4(Registers->cr4);

	//
	// Enable VMX Root Mode
	//
	if (__vmx_on(&launch_context->vmx_on_physical_address))
	{
		return FALSE;
	}

	//
	// Clear the state of the VMCS, setting it to Inactive
	//
	if (__vmx_vmclear(&launch_context->vmcs_physical_address))
	{
		__vmx_off();
		return FALSE;
	}

	//
	// Load the VMCS, setting its state to Active
	//
	if (__vmx_vmptrld(&launch_context->vmcs_physical_address))
	{
		__vmx_off();
		return FALSE;
	}

	//
	// VMX Root Mode is enabled, with an active VMCS.
	//
	return TRUE;
}


VOID
ShvUtilConvertGdtEntry(
	_In_ uint64_t GdtBase,
	_In_ UINT16 Selector,
	_Out_ vmx::gdt_entry* VmxGdtEntry
)
{
	//
	// Reject LDT or NULL entries
	//
	if ((Selector == 0) ||
		(Selector & SEGMENT_SELECTOR_TABLE_FLAG) != 0)
	{
		VmxGdtEntry->limit = 0;
		VmxGdtEntry->access_rights.flags = 0;
		VmxGdtEntry->base = 0;
		VmxGdtEntry->selector = 0;
		VmxGdtEntry->access_rights.unusable = 1;
		return;
	}

	//
	// Read the GDT entry at the given selector, masking out the RPL bits.
	//
	auto* gdt_entry = (segment_descriptor_64*)(GdtBase + (Selector & ~
		SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK));

	//
	// Write the selector directly 
	//
	VmxGdtEntry->selector = Selector;

	//
	// Use the LSL intrinsic to read the segment limit
	//
	VmxGdtEntry->limit = __segmentlimit(Selector);

	//
	// Build the full 64-bit effective address, keeping in mind that only when
	// the System bit is unset, should this be done.
	//
	// NOTE: The Windows definition of KGDTENTRY64 is WRONG. The "System" field
	// is incorrectly defined at the position of where the AVL bit should be.
	// The actual location of the SYSTEM bit is encoded as the highest bit in
	// the "Type" field.
	//
	VmxGdtEntry->base = 0;
	VmxGdtEntry->base |= static_cast<uint64_t>(gdt_entry->base_address_low);
	VmxGdtEntry->base |= static_cast<uint64_t>(gdt_entry->base_address_middle) << 16;
	VmxGdtEntry->base |= static_cast<uint64_t>(gdt_entry->base_address_high) << 24;
	if (gdt_entry->descriptor_type == 0u)
	{
		VmxGdtEntry->base |= static_cast<uint64_t>(gdt_entry->base_address_upper) << 32;
	}

	//
	// Load the access rights
	//
	VmxGdtEntry->access_rights.flags = 0;

	VmxGdtEntry->access_rights.type = gdt_entry->type;
	VmxGdtEntry->access_rights.descriptor_type = gdt_entry->descriptor_type;
	VmxGdtEntry->access_rights.descriptor_privilege_level = gdt_entry->descriptor_privilege_level;
	VmxGdtEntry->access_rights.present = gdt_entry->present;
	VmxGdtEntry->access_rights.reserved1 = gdt_entry->segment_limit_high;
	VmxGdtEntry->access_rights.available_bit = gdt_entry->system;
	VmxGdtEntry->access_rights.long_mode = gdt_entry->long_mode;
	VmxGdtEntry->access_rights.default_big = gdt_entry->default_big;
	VmxGdtEntry->access_rights.granularity = gdt_entry->granularity;

	//
	// Finally, handle the VMX-specific bits
	//
	VmxGdtEntry->access_rights.reserved1 = 0;
	VmxGdtEntry->access_rights.unusable = !gdt_entry->present;
}

UINT32
ShvUtilAdjustMsr(
	_In_ LARGE_INTEGER ControlValue,
	_In_ UINT32 DesiredValue
)
{
	//
	// VMX feature/capability MSRs encode the "must be 0" bits in the high word
	// of their value, and the "must be 1" bits in the low word of their value.
	// Adjust any requested capability/feature based on these requirements.
	//
	DesiredValue &= ControlValue.HighPart;
	DesiredValue |= ControlValue.LowPart;
	return DesiredValue;
}

void vmx_handle_invd()
{
	//
	// This is the handler for the INVD instruction. Technically it may be more
	// correct to use __invd instead of __wbinvd, but that intrinsic doesn't
	// actually exist. Additionally, the Windows kernel (or HAL) don't contain
	// any example of INVD actually ever being used. Finally, Hyper-V itself
	// handles INVD by issuing WBINVD as well, so we'll just do that here too.
	//
	__wbinvd();
}

#define DPL_USER                3
#define DPL_SYSTEM              0

void vmx_handle_cpuid(vmx::guest_context& guest_context)
{
	INT32 cpu_info[4];

	//
	// Check for the magic CPUID sequence, and check that it is coming from
	// Ring 0. Technically we could also check the RIP and see if this falls
	// in the expected function, but we may want to allow a separate "unload"
	// driver or code at some point.
	//
	if ((guest_context.vp_regs->Rax == 0x41414141) &&
		(guest_context.vp_regs->Rcx == 0x42424242) &&
		((read_vmx(VMCS_GUEST_CS_SELECTOR) & SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK) == DPL_SYSTEM))
	{
		guest_context.exit_vm = true;
		return;
	}

	//
	// Otherwise, issue the CPUID to the logical processor based on the indexes
	// on the VP's GPRs.
	//
	__cpuidex(cpu_info, (INT32)guest_context.vp_regs->Rax, (INT32)guest_context.vp_regs->Rcx);

	//
	// Check if this was CPUID 1h, which is the features request.
	//
	if (guest_context.vp_regs->Rax == 1)
	{
		//
		// Set the Hypervisor Present-bit in RCX, which Intel and AMD have both
		// reserved for this indication.
		//
		cpu_info[2] |= HYPERV_HYPERVISOR_PRESENT_BIT;
	}
	else if (guest_context.vp_regs->Rax == HYPERV_CPUID_INTERFACE)
	{
		//
		// Return our interface identifier
		//
		cpu_info[0] = 'momo';
	}

	//
	// Copy the values from the logical processor registers into the VP GPRs.
	//
	guest_context.vp_regs->Rax = cpu_info[0];
	guest_context.vp_regs->Rbx = cpu_info[1];
	guest_context.vp_regs->Rcx = cpu_info[2];
	guest_context.vp_regs->Rdx = cpu_info[3];
}

void vmx_handle_xsetbv(const vmx::guest_context& guest_contex)
{
	//
	// Simply issue the XSETBV instruction on the native logical processor.
	//

	_xsetbv(static_cast<uint32_t>(guest_contex.vp_regs->Rcx),
	        guest_contex.vp_regs->Rdx << 32 | guest_contex.vp_regs->Rax);
}

void vmx_handle_vmx(vmx::guest_context& guest_contex)
{
	//
	// Set the CF flag, which is how VMX instructions indicate failure
	//
	guest_contex.guest_e_flags |= 0x1; // VM_FAIL_INVALID

	//
	// RFLAGs is actually restored from the VMCS, so update it here
	//
	__vmx_vmwrite(VMCS_GUEST_RFLAGS, guest_contex.guest_e_flags);
}

void vmx_dispatch_vm_exit(vmx::guest_context& guest_contex)
{
	//
	// This is the generic VM-Exit handler. Decode the reason for the exit and
	// call the appropriate handler. As per Intel specifications, given that we
	// have requested no optional exits whatsoever, we should only see CPUID,
	// INVD, XSETBV and other VMX instructions. GETSEC cannot happen as we do
	// not run in SMX context.
	//
	switch (guest_contex.exit_reason)
	{
	case VMX_EXIT_REASON_EXECUTE_CPUID:
		vmx_handle_cpuid(guest_contex);
		break;
	case VMX_EXIT_REASON_EXECUTE_INVD:
		vmx_handle_invd();
		break;
	case VMX_EXIT_REASON_EXECUTE_XSETBV:
		vmx_handle_xsetbv(guest_contex);
		break;
	case VMX_EXIT_REASON_EXECUTE_VMCALL:
	case VMX_EXIT_REASON_EXECUTE_VMCLEAR:
	case VMX_EXIT_REASON_EXECUTE_VMLAUNCH:
	case VMX_EXIT_REASON_EXECUTE_VMPTRLD:
	case VMX_EXIT_REASON_EXECUTE_VMPTRST:
	case VMX_EXIT_REASON_EXECUTE_VMREAD:
	case VMX_EXIT_REASON_EXECUTE_VMRESUME:
	case VMX_EXIT_REASON_EXECUTE_VMWRITE:
	case VMX_EXIT_REASON_EXECUTE_VMXOFF:
	case VMX_EXIT_REASON_EXECUTE_VMXON:
		vmx_handle_vmx(guest_contex);
		break;
	default:
		break;
	}

	//
	// Move the instruction pointer to the next instruction after the one that
	// caused the exit. Since we are not doing any special handling or changing
	// of execution, this can be done for any exit reason.
	//
	guest_contex.guest_rip += read_vmx(VMCS_VMEXIT_INSTRUCTION_LENGTH);
	__vmx_vmwrite(VMCS_GUEST_RIP, guest_contex.guest_rip);
}

extern "C" [[ noreturn ]] void vm_exit_handler(CONTEXT* context)
{
	auto* vm_state = resolve_vm_state_from_context(*context);

	//
	// Build a little stack context to make it easier to keep track of certain
	// guest state, such as the RIP/RSP/RFLAGS, and the exit reason. The rest
	// of the general purpose registers come from the context structure that we
	// captured on our own with RtlCaptureContext in the assembly entrypoint.
	//
	vmx::guest_context guest_context{};
	guest_context.guest_e_flags = read_vmx(VMCS_GUEST_RFLAGS);
	guest_context.guest_rip = read_vmx(VMCS_GUEST_RIP);
	guest_context.guest_rsp = read_vmx(VMCS_GUEST_RSP);
	guest_context.exit_reason = read_vmx(VMCS_EXIT_REASON) & 0xFFFF;
	guest_context.vp_regs = context;
	guest_context.exit_vm = false;

	//
	// Call the generic handler
	//
	vmx_dispatch_vm_exit(guest_context);

	//
	// Did we hit the magic exit sequence, or should we resume back to the VM
	// context?
	//
	if (guest_context.exit_vm)
	{
		context->Rcx = 0x43434343;

		//
		// Perform any OS-specific CPU uninitialization work
		//
		restore_descriptor_tables(vm_state->launch_context);

		//
		// Our callback routine may have interrupted an arbitrary user process,
		// and therefore not a thread running with a systemwide page directory.
		// Therefore if we return back to the original caller after turning off
		// VMX, it will keep our current "host" CR3 value which we set on entry
		// to the PML4 of the SYSTEM process. We want to return back with the
		// correct value of the "guest" CR3, so that the currently executing
		// process continues to run with its expected address space mappings.
		//
		__writecr3(read_vmx(VMCS_GUEST_CR3));

		//
		// Finally, restore the stack, instruction pointer and EFLAGS to the
		// original values present when the instruction causing our VM-Exit
		// execute (such as ShvVpUninitialize). This will effectively act as
		// a longjmp back to that location.
		//
		context->Rsp = guest_context.guest_rsp;
		context->Rip = guest_context.guest_rip;
		context->EFlags = static_cast<uint32_t>(guest_context.guest_e_flags);

		//
		// Turn off VMX root mode on this logical processor. We're done here.
		//
		__vmx_off();
	}
	else
	{
		//
		// Return into a VMXRESUME intrinsic, which we broke out as its own
		// function, in order to allow this to work. No assembly code will be
		// needed as RtlRestoreContext will fix all the GPRs, and what we just
		// did to RSP will take care of the rest.
		//
		context->Rip = reinterpret_cast<uint64_t>(resume_vmx);
	}

	//
	// Restore the context to either ShvVmxResume, in which case the CPU's VMX
	// facility will do the "true" return back to the VM (but without restoring
	// GPRs, which is why we must do it here), or to the original guest's RIP,
	// which we use in case an exit was requested. In this case VMX must now be
	// off, and this will look like a longjmp to the original stack and RIP.
	//
	restore_context(context);
}

void ShvVmxSetupVmcsForVp(vmx::state* VpData)
{
	auto* launch_context = &VpData->launch_context;
	auto* state = &launch_context->special_registers;
	auto* context = &launch_context->context_frame;

	//
	// Begin by setting the link pointer to the required value for 4KB VMCS.
	//
	__vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, ~0ULL);

	//
	// Enable EPT features if supported
	//
	if (launch_context->ept_controls.flags != 0)
	{
		ept_pointer vmx_eptp{};
		vmx_eptp.flags = 0;
		vmx_eptp.page_walk_length = 3;
		vmx_eptp.memory_type = MEMORY_TYPE_WRITE_BACK;
		vmx_eptp.page_frame_number = launch_context->ept_pml4_physical_address / PAGE_SIZE;

		__vmx_vmwrite(VMCS_CTRL_EPT_POINTER, vmx_eptp.flags);
		__vmx_vmwrite(VMCS_CTRL_VIRTUAL_PROCESSOR_IDENTIFIER, 1);
	}

	//
	// Load the MSR bitmap. Unlike other bitmaps, not having an MSR bitmap will
	// trap all MSRs, so we allocated an empty one.
	//
	__vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, launch_context->msr_bitmap_physical_address);

	//
	// Enable support for RDTSCP and XSAVES/XRESTORES in the guest. Windows 10
	// makes use of both of these instructions if the CPU supports it. By using
	// ShvUtilAdjustMsr, these options will be ignored if this processor does
	// not actually support the instructions to begin with.
	//
	// Also enable EPT support, for additional performance and ability to trap
	// memory access efficiently.
	//
	auto ept_controls = launch_context->ept_controls;
	ept_controls.enable_rdtscp = 1;
	ept_controls.enable_invpcid = 1;
	ept_controls.enable_xsaves = 1;
	__vmx_vmwrite(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
	              ShvUtilAdjustMsr(launch_context->msr_data[11], ept_controls.flags));

	//
	// Enable no pin-based options ourselves, but there may be some required by
	// the processor. Use ShvUtilAdjustMsr to add those in.
	//
	__vmx_vmwrite(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS,
	              ShvUtilAdjustMsr(launch_context->msr_data[13], 0));

	//
	// In order for our choice of supporting RDTSCP and XSAVE/RESTORES above to
	// actually mean something, we have to request secondary controls. We also
	// want to activate the MSR bitmap in order to keep them from being caught.
	//
	ia32_vmx_procbased_ctls_register procbased_ctls_register{};
	procbased_ctls_register.activate_secondary_controls = 1;
	procbased_ctls_register.use_msr_bitmaps = 1;

	__vmx_vmwrite(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
	              ShvUtilAdjustMsr(launch_context->msr_data[14],
	                               procbased_ctls_register.flags));

	//
	// Make sure to enter us in x64 mode at all times.
	//
	ia32_vmx_exit_ctls_register exit_ctls_register{};
	exit_ctls_register.host_address_space_size = 1;
	__vmx_vmwrite(VMCS_CTRL_VMEXIT_CONTROLS,
	              ShvUtilAdjustMsr(launch_context->msr_data[15],
	                               exit_ctls_register.flags));

	//
	// As we exit back into the guest, make sure to exist in x64 mode as well.
	//
	ia32_vmx_entry_ctls_register entry_ctls_register{};
	entry_ctls_register.ia32e_mode_guest = 1;
	__vmx_vmwrite(VMCS_CTRL_VMENTRY_CONTROLS,
	              ShvUtilAdjustMsr(launch_context->msr_data[16],
	                               entry_ctls_register.flags));

	//
	// Load the CS Segment (Ring 0 Code)
	//
	vmx::gdt_entry vmx_gdt_entry{};
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegCs, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_CS_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_CS_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_CS_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_CS_SELECTOR, context->SegCs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the SS Segment (Ring 0 Data)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegSs, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_SS_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_SS_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_SS_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_SS_SELECTOR, context->SegSs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the DS Segment (Ring 3 Data)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegDs, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_DS_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_DS_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_DS_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_DS_SELECTOR, context->SegDs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the ES Segment (Ring 3 Data)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegEs, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_ES_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_ES_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_ES_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_ES_SELECTOR, context->SegEs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the FS Segment (Ring 3 Compatibility-Mode TEB)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegFs, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_FS_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_FS_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_FS_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_FS_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_FS_SELECTOR, context->SegFs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the GS Segment (Ring 3 Data if in Compatibility-Mode, MSR-based in Long Mode)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegGs, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_GS_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_GS_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_GS_BASE, state->msr_gs_base);
	__vmx_vmwrite(VMCS_HOST_GS_BASE, state->msr_gs_base);
	__vmx_vmwrite(VMCS_HOST_GS_SELECTOR, context->SegGs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the Task Register (Ring 0 TSS)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, state->tr, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_TR_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_TR_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_TR_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_TR_BASE, vmx_gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_TR_SELECTOR, state->tr & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the Local Descriptor Table (Ring 0 LDT on Redstone)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, state->ldtr, &vmx_gdt_entry);
	__vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, vmx_gdt_entry.selector);
	__vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, vmx_gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, vmx_gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_LDTR_BASE, vmx_gdt_entry.base);

	//
	// Now load the GDT itself
	//
	__vmx_vmwrite(VMCS_GUEST_GDTR_BASE, state->gdtr.base_address);
	__vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, state->gdtr.limit);
	__vmx_vmwrite(VMCS_HOST_GDTR_BASE, state->gdtr.base_address);

	//
	// And then the IDT
	//
	__vmx_vmwrite(VMCS_GUEST_IDTR_BASE, state->idtr.base_address);
	__vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, state->idtr.limit);
	__vmx_vmwrite(VMCS_HOST_IDTR_BASE, state->idtr.base_address);

	//
	// Load CR0
	//
	__vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, state->cr0);
	__vmx_vmwrite(VMCS_HOST_CR0, state->cr0);
	__vmx_vmwrite(VMCS_GUEST_CR0, state->cr0);

	//
	// Load CR3 -- do not use the current process' address space for the host,
	// because we may be executing in an arbitrary user-mode process right now
	// as part of the DPC interrupt we execute in.
	//
	__vmx_vmwrite(VMCS_HOST_CR3, launch_context->system_directory_table_base);
	__vmx_vmwrite(VMCS_GUEST_CR3, state->cr3);

	//
	// Load CR4
	//
	__vmx_vmwrite(VMCS_HOST_CR4, state->cr4);
	__vmx_vmwrite(VMCS_GUEST_CR4, state->cr4);
	__vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, state->cr4);

	//
	// Load debug MSR and register (DR7)
	//
	__vmx_vmwrite(VMCS_GUEST_DEBUGCTL, state->debug_control);
	__vmx_vmwrite(VMCS_GUEST_DR7, state->kernel_dr7);

	//
	// Finally, load the guest stack, instruction pointer, and rflags, which
	// corresponds exactly to the location where RtlCaptureContext will return
	// to inside of ShvVpInitialize.
	//
	const auto stack_pointer = reinterpret_cast<uintptr_t>(VpData->stack_buffer) + KERNEL_STACK_SIZE - sizeof(CONTEXT);

	__vmx_vmwrite(VMCS_GUEST_RSP, stack_pointer);
	__vmx_vmwrite(VMCS_GUEST_RIP, reinterpret_cast<uintptr_t>(vm_launch));
	__vmx_vmwrite(VMCS_GUEST_RFLAGS, context->EFlags);

	//
	// Load the hypervisor entrypoint and stack. We give ourselves a standard
	// size kernel stack (24KB) and bias for the context structure that the
	// hypervisor entrypoint will push on the stack, avoiding the need for RSP
	// modifying instructions in the entrypoint. Note that the CONTEXT pointer
	// and thus the stack itself, must be 16-byte aligned for ABI compatibility
	// with AMD64 -- specifically, XMM operations will fail otherwise, such as
	// the ones that RtlCaptureContext will perform.
	//
	C_ASSERT((KERNEL_STACK_SIZE - sizeof(CONTEXT)) % 16 == 0);
	__vmx_vmwrite(VMCS_HOST_RSP, stack_pointer);
	__vmx_vmwrite(VMCS_HOST_RIP, reinterpret_cast<uintptr_t>(vm_exit));
}

void initialize_msrs(vmx::launch_context& launch_context)
{
	constexpr auto msr_count = sizeof(launch_context.msr_data) / sizeof(launch_context.msr_data[0]);
	for (auto i = 0u; i < msr_count; ++i)
	{
		launch_context.msr_data[i].QuadPart = __readmsr(IA32_VMX_BASIC + i);
	}
}

void launch_hypervisor(vmx::state& vm_state)
{
	initialize_msrs(vm_state.launch_context);
	ShvVmxMtrrInitialize(&vm_state);
	ShvVmxEptInitialize(&vm_state);

	if (!ShvVmxEnterRootModeOnVp(&vm_state))
	{
		throw std::runtime_error("Not available");
	}

	ShvVmxSetupVmcsForVp(&vm_state);

	auto error_code = launch_vmx();
	throw std::runtime_error(string::va("Failed to launch vmx: %X", error_code));
}


void hypervisor::enable_core(const uint64_t system_directory_table_base)
{
	debug_log("Enabling hypervisor on core %d\n", thread::get_processor_index());
	auto* vm_state = this->get_current_vm_state();

	vm_state->launch_context.system_directory_table_base = system_directory_table_base;

	capture_cpu_context(vm_state->launch_context);

	const rflags rflags{.flags = __readeflags()};
	if (!rflags.alignment_check_flag)
	{
		launch_hypervisor(*vm_state);
	}

	if (!is_hypervisor_present())
	{
		throw std::runtime_error("Hypervisor is not present");
	}
}

void hypervisor::disable_core()
{
	debug_log("Disabling hypervisor on core %d\n", thread::get_processor_index());

	int32_t cpu_info[4]{0};
	__cpuidex(cpu_info, 0x41414141, 0x42424242);

	if (this->is_enabled())
	{
		debug_log("Shutdown for core %d failed. Issuing kernel panic!\n", thread::get_processor_index());
		KeBugCheckEx(DRIVER_VIOLATION, 1, 0, 0, 0);
	}
}

void hypervisor::allocate_vm_states()
{
	if (this->vm_states_)
	{
		throw std::runtime_error("VM states are still in use");
	}

	// As Windows technically supports cpu hot-plugging, keep track of the allocation count.
	// However virtualizing the hot-plugged cpu won't be supported here.
	this->vm_state_count_ = thread::get_processor_count();
	this->vm_states_ = new vmx::state*[this->vm_state_count_]{};

	for (auto i = 0u; i < this->vm_state_count_; ++i)
	{
		this->vm_states_[i] = memory::allocate_aligned_object<vmx::state>();
		if (!this->vm_states_[i])
		{
			throw std::runtime_error("Failed to allocate VM state entries");
		}
	}
}

void hypervisor::free_vm_states()
{
	if (!this->vm_states_)
	{
		return;
	}

	for (auto i = 0u; i < this->vm_state_count_; ++i)
	{
		memory::free_aligned_memory(this->vm_states_[i]);
	}

	delete[] this->vm_states_;
	this->vm_states_ = nullptr;
	this->vm_state_count_ = 0;
}

vmx::state* hypervisor::get_current_vm_state() const
{
	const auto current_core = thread::get_processor_index();
	if (current_core >= this->vm_state_count_)
	{
		return nullptr;
	}

	return this->vm_states_[current_core];
}
