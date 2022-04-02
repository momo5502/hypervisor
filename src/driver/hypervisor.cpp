#include "std_include.hpp"
#include "hypervisor.hpp"

#include "exception.hpp"
#include "logging.hpp"
#include "finally.hpp"
#include "memory.hpp"
#include "thread.hpp"
#include "assembly.hpp"

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

void ShvCaptureSpecialRegisters(vmx::special_registers* special_registers)
{
	special_registers->cr0 = __readcr0();
	special_registers->cr3 = __readcr3();
	special_registers->cr4 = __readcr4();
	special_registers->debug_control = __readmsr(IA32_DEBUGCTL);
	special_registers->msr_gs_base = __readmsr(IA32_GS_BASE);
	special_registers->kernel_dr7 = __readdr(7);
	_sgdt(&special_registers->gdtr.limit);
	__sidt(&special_registers->idtr.limit);
	_str(&special_registers->tr);
	_sldt(&special_registers->ldtr);
}

uintptr_t
FORCEINLINE
ShvVmxRead(
	_In_ UINT32 VmcsFieldId
)
{
	size_t FieldData;

	//
	// Because VMXREAD returns an error code, and not the data, it is painful
	// to use in most circumstances. This simple function simplifies it use.
	//
	__vmx_vmread(VmcsFieldId, &FieldData);
	return FieldData;
}

INT32
ShvVmxLaunch(
	VOID)
{
	INT32 failureCode;

	//
	// Launch the VMCS
	//
	__vmx_vmlaunch();

	//
	// If we got here, either VMCS setup failed in some way, or the launch
	// did not proceed as planned.
	//
	failureCode = (INT32)ShvVmxRead(VMCS_VM_INSTRUCTION_ERROR);
	__vmx_off();

	//
	// Return the error back to the caller
	//
	return failureCode;
}

#define MTRR_PAGE_SIZE          4096
#define MTRR_PAGE_MASK          (~(MTRR_PAGE_SIZE-1))

VOID ShvVmxMtrrInitialize(vmx::vm_state* VpData)
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
	vmx::vm_state* VpData,
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

void ShvVmxEptInitialize(vmx::vm_state* VpData)
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


UINT8
ShvVmxEnterRootModeOnVp(vmx::vm_state* VpData)
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
	_Out_ vmx_gdt_entry* VmxGdtEntry
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

extern "C" VOID
ShvOsCaptureContext(
	_In_ PCONTEXT ContextRecord
)
{
	//
	// Windows provides a nice OS function to do this
	//
	RtlCaptureContext(ContextRecord);
}

extern "C" DECLSPEC_NORETURN
VOID
__cdecl
ShvOsRestoreContext2(
	_In_ PCONTEXT ContextRecord,
	_In_opt_ struct _EXCEPTION_RECORD* ExceptionRecord
);

DECLSPEC_NORETURN
VOID
ShvVpRestoreAfterLaunch(
	VOID)
{
	debug_log("[%d] restore\n", thread::get_processor_index());
	//
	// Get the per-processor data. This routine temporarily executes on the
	// same stack as the hypervisor (using no real stack space except the home
	// registers), so we can retrieve the VP the same way the hypervisor does.
	//
	auto* vpData = (vmx::vm_state*)((uintptr_t)_AddressOfReturnAddress() +
		sizeof(CONTEXT) -
		KERNEL_STACK_SIZE);

	//
	// Record that VMX is now enabled by returning back to ShvVpInitialize with
	// the Alignment Check (AC) bit set.
	//
	vpData->launch_context.context_frame.EFlags |= EFLAGS_ALIGNMENT_CHECK_FLAG_FLAG;

	//
	// And finally, restore the context, so that all register and stack
	// state is finally restored.
	//
	ShvOsRestoreContext2(&vpData->launch_context.context_frame, nullptr);
}


VOID
ShvVmxHandleInvd(
	VOID)
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

typedef struct _SHV_VP_STATE
{
	PCONTEXT VpRegs;
	uintptr_t GuestRip;
	uintptr_t GuestRsp;
	uintptr_t GuestEFlags;
	UINT16 ExitReason;
	UINT8 ExitVm;
} SHV_VP_STATE, *PSHV_VP_STATE;

VOID
ShvVmxHandleCpuid(
	_In_ PSHV_VP_STATE VpState
)
{
	INT32 cpu_info[4];

	//
	// Check for the magic CPUID sequence, and check that it is coming from
	// Ring 0. Technically we could also check the RIP and see if this falls
	// in the expected function, but we may want to allow a separate "unload"
	// driver or code at some point.
	//
	if ((VpState->VpRegs->Rax == 0x41414141) &&
		(VpState->VpRegs->Rcx == 0x42424242) &&
		((ShvVmxRead(VMCS_GUEST_CS_SELECTOR) & SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK) == DPL_SYSTEM))
	{
		VpState->ExitVm = TRUE;
		return;
	}

	//
	// Otherwise, issue the CPUID to the logical processor based on the indexes
	// on the VP's GPRs.
	//
	__cpuidex(cpu_info, (INT32)VpState->VpRegs->Rax, (INT32)VpState->VpRegs->Rcx);

	//
	// Check if this was CPUID 1h, which is the features request.
	//
	if (VpState->VpRegs->Rax == 1)
	{
		//
		// Set the Hypervisor Present-bit in RCX, which Intel and AMD have both
		// reserved for this indication.
		//
		cpu_info[2] |= HYPERV_HYPERVISOR_PRESENT_BIT;
	}
	else if (VpState->VpRegs->Rax == HYPERV_CPUID_INTERFACE)
	{
		//
		// Return our interface identifier
		//
		cpu_info[0] = 'momo';
	}

	//
	// Copy the values from the logical processor registers into the VP GPRs.
	//
	VpState->VpRegs->Rax = cpu_info[0];
	VpState->VpRegs->Rbx = cpu_info[1];
	VpState->VpRegs->Rcx = cpu_info[2];
	VpState->VpRegs->Rdx = cpu_info[3];
}

VOID
ShvVmxHandleXsetbv(
	_In_ PSHV_VP_STATE VpState
)
{
	//
	// Simply issue the XSETBV instruction on the native logical processor.
	//

	_xsetbv((UINT32)VpState->VpRegs->Rcx,
	        VpState->VpRegs->Rdx << 32 |
	        VpState->VpRegs->Rax);
}

VOID
ShvVmxHandleVmx(
	_In_ PSHV_VP_STATE VpState
)
{
	//
	// Set the CF flag, which is how VMX instructions indicate failure
	//
	VpState->GuestEFlags |= 0x1; // VM_FAIL_INVALID

	//
	// RFLAGs is actually restored from the VMCS, so update it here
	//
	__vmx_vmwrite(VMCS_GUEST_RFLAGS, VpState->GuestEFlags);
}

VOID
ShvVmxHandleExit(
	_In_ PSHV_VP_STATE VpState
)
{
	//
	// This is the generic VM-Exit handler. Decode the reason for the exit and
	// call the appropriate handler. As per Intel specifications, given that we
	// have requested no optional exits whatsoever, we should only see CPUID,
	// INVD, XSETBV and other VMX instructions. GETSEC cannot happen as we do
	// not run in SMX context.
	//
	switch (VpState->ExitReason)
	{
	case VMX_EXIT_REASON_EXECUTE_CPUID:
		ShvVmxHandleCpuid(VpState);
		break;
	case VMX_EXIT_REASON_EXECUTE_INVD:
		ShvVmxHandleInvd();
		break;
	case VMX_EXIT_REASON_EXECUTE_XSETBV:
		ShvVmxHandleXsetbv(VpState);
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
		ShvVmxHandleVmx(VpState);
		break;
	default:
		break;
	}

	//
	// Move the instruction pointer to the next instruction after the one that
	// caused the exit. Since we are not doing any special handling or changing
	// of execution, this can be done for any exit reason.
	//
	VpState->GuestRip += ShvVmxRead(VMCS_VMEXIT_INSTRUCTION_LENGTH);
	__vmx_vmwrite(VMCS_GUEST_RIP, VpState->GuestRip);
}

VOID
ShvOsUnprepareProcessor(
	_In_ vmx::vm_state* VpData
)
{
	//
	// When running in VMX root mode, the processor will set limits of the
	// GDT and IDT to 0xFFFF (notice that there are no Host VMCS fields to
	// set these values). This causes problems with PatchGuard, which will
	// believe that the GDTR and IDTR have been modified by malware, and
	// eventually crash the system. Since we know what the original state
	// of the GDTR and IDTR was, simply restore it now.
	//
	__lgdt(&VpData->launch_context.special_registers.gdtr.limit);
	__lidt(&VpData->launch_context.special_registers.idtr.limit);
}

DECLSPEC_NORETURN
VOID
ShvVmxResume()
{
	//
	// Issue a VMXRESUME. The reason that we've defined an entire function for
	// this sole instruction is both so that we can use it as the target of the
	// VMCS when re-entering the VM After a VM-Exit, as well as so that we can
	// decorate it with the DECLSPEC_NORETURN marker, which is not set on the
	// intrinsic (as it can fail in case of an error).
	//
	__vmx_vmresume();
}

extern "C" DECLSPEC_NORETURN
VOID
ShvVmxEntryHandler()
{
	PCONTEXT Context = (PCONTEXT)_AddressOfReturnAddress();
	SHV_VP_STATE guestContext;

	//
	// Because we had to use RCX when calling ShvOsCaptureContext, its value
	// was actually pushed on the stack right before the call. Go dig into the
	// stack to find it, and overwrite the bogus value that's there now.
	//
	//Context->Rcx = *(UINT64*)((uintptr_t)Context - sizeof(Context->Rcx));

	//
	// Get the per-VP data for this processor.
	//
	auto* vpData = (vmx::vm_state*)((uintptr_t)(Context + 1) - KERNEL_STACK_SIZE);

	//
	// Build a little stack context to make it easier to keep track of certain
	// guest state, such as the RIP/RSP/RFLAGS, and the exit reason. The rest
	// of the general purpose registers come from the context structure that we
	// captured on our own with RtlCaptureContext in the assembly entrypoint.
	//
	guestContext.GuestEFlags = ShvVmxRead(VMCS_GUEST_RFLAGS);
	guestContext.GuestRip = ShvVmxRead(VMCS_GUEST_RIP);
	guestContext.GuestRsp = ShvVmxRead(VMCS_GUEST_RSP);
	guestContext.ExitReason = ShvVmxRead(VMCS_EXIT_REASON) & 0xFFFF;
	guestContext.VpRegs = Context;
	guestContext.ExitVm = FALSE;

	//
	// Call the generic handler
	//
	ShvVmxHandleExit(&guestContext);

	//
	// Did we hit the magic exit sequence, or should we resume back to the VM
	// context?
	//
	if (guestContext.ExitVm != FALSE)
	{
		//
		// Return the VP Data structure in RAX:RBX which is going to be part of
		// the CPUID response that the caller (ShvVpUninitialize) expects back.
		// Return confirmation in RCX that we are loaded
		//
		Context->Rax = (uintptr_t)vpData >> 32;
		Context->Rbx = (uintptr_t)vpData & 0xFFFFFFFF;
		Context->Rcx = 0x43434343;

		//
		// Perform any OS-specific CPU uninitialization work
		//
		ShvOsUnprepareProcessor(vpData);

		//
		// Our callback routine may have interrupted an arbitrary user process,
		// and therefore not a thread running with a systemwide page directory.
		// Therefore if we return back to the original caller after turning off
		// VMX, it will keep our current "host" CR3 value which we set on entry
		// to the PML4 of the SYSTEM process. We want to return back with the
		// correct value of the "guest" CR3, so that the currently executing
		// process continues to run with its expected address space mappings.
		//
		__writecr3(ShvVmxRead(VMCS_GUEST_CR3));

		//
		// Finally, restore the stack, instruction pointer and EFLAGS to the
		// original values present when the instruction causing our VM-Exit
		// execute (such as ShvVpUninitialize). This will effectively act as
		// a longjmp back to that location.
		//
		Context->Rsp = guestContext.GuestRsp;
		Context->Rip = (UINT64)guestContext.GuestRip;
		Context->EFlags = (UINT32)guestContext.GuestEFlags;

		//
		// Turn off VMX root mode on this logical processor. We're done here.
		//
		__vmx_off();
	}
	else
	{
		//
		// Because we won't be returning back into assembly code, nothing will
		// ever know about the "pop rcx" that must technically be done (or more
		// accurately "add rsp, 4" as rcx will already be correct thanks to the
		// fixup earlier. In order to keep the stack sane, do that adjustment
		// here.
		//
		//Context->Rsp += sizeof(Context->Rcx);

		//
		// Return into a VMXRESUME intrinsic, which we broke out as its own
		// function, in order to allow this to work. No assembly code will be
		// needed as RtlRestoreContext will fix all the GPRs, and what we just
		// did to RSP will take care of the rest.
		//
		Context->Rip = (UINT64)ShvVmxResume;
	}

	//
	// Restore the context to either ShvVmxResume, in which case the CPU's VMX
	// facility will do the "true" return back to the VM (but without restoring
	// GPRs, which is why we must do it here), or to the original guest's RIP,
	// which we use in case an exit was requested. In this case VMX must now be
	// off, and this will look like a longjmp to the original stack and RIP.
	//
	ShvOsRestoreContext2(Context, nullptr);
}


extern "C" VOID
ShvVmxEntry(
	VOID);

void ShvVmxSetupVmcsForVp(vmx::vm_state* VpData)
{
	auto* launch_context = &VpData->launch_context;
	auto* state = &launch_context->special_registers;
	PCONTEXT context = &launch_context->context_frame;
	vmx_gdt_entry vmxGdtEntry;
	ept_pointer vmxEptp;

	//
	// Begin by setting the link pointer to the required value for 4KB VMCS.
	//
	__vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, ~0ULL);

	//
	// Enable EPT features if supported
	//
	if (launch_context->ept_controls.flags != 0)
	{
		//
		// Configure the EPTP
		//
		vmxEptp.flags = 0;
		vmxEptp.page_walk_length = 3;
		vmxEptp.memory_type = MEMORY_TYPE_WRITE_BACK;
		vmxEptp.page_frame_number = launch_context->ept_pml4_physical_address / PAGE_SIZE;

		//
		// Load EPT Root Pointer
		//
		__vmx_vmwrite(VMCS_CTRL_EPT_POINTER, vmxEptp.flags);

		//
		// Set VPID to one
		//
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
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegCs, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_CS_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_CS_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_CS_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_CS_SELECTOR, context->SegCs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the SS Segment (Ring 0 Data)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegSs, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_SS_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_SS_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_SS_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_SS_SELECTOR, context->SegSs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the DS Segment (Ring 3 Data)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegDs, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_DS_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_DS_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_DS_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_DS_SELECTOR, context->SegDs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the ES Segment (Ring 3 Data)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegEs, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_ES_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_ES_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_ES_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_ES_SELECTOR, context->SegEs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the FS Segment (Ring 3 Compatibility-Mode TEB)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegFs, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_FS_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_FS_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_FS_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_FS_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_FS_SELECTOR, context->SegFs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the GS Segment (Ring 3 Data if in Compatibility-Mode, MSR-based in Long Mode)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, context->SegGs, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_GS_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_GS_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_GS_BASE, state->msr_gs_base);
	__vmx_vmwrite(VMCS_HOST_GS_BASE, state->msr_gs_base);
	__vmx_vmwrite(VMCS_HOST_GS_SELECTOR, context->SegGs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the Task Register (Ring 0 TSS)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, state->tr, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_TR_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_TR_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_TR_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_TR_BASE, vmxGdtEntry.base);
	__vmx_vmwrite(VMCS_HOST_TR_SELECTOR, state->tr & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	//
	// Load the Local Descriptor Table (Ring 0 LDT on Redstone)
	//
	ShvUtilConvertGdtEntry(state->gdtr.base_address, state->ldtr, &vmxGdtEntry);
	__vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, vmxGdtEntry.selector);
	__vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, vmxGdtEntry.limit);
	__vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, vmxGdtEntry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_LDTR_BASE, vmxGdtEntry.base);

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
	__vmx_vmwrite(VMCS_GUEST_RSP, (uintptr_t)VpData->stack_buffer + KERNEL_STACK_SIZE - sizeof(CONTEXT));
	__vmx_vmwrite(VMCS_GUEST_RIP, (uintptr_t)ShvVpRestoreAfterLaunch);
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
	__vmx_vmwrite(VMCS_HOST_RSP, (uintptr_t)VpData->stack_buffer + KERNEL_STACK_SIZE - sizeof(CONTEXT));
	__vmx_vmwrite(VMCS_HOST_RIP, (uintptr_t)ShvVmxEntry);
}

INT32 ShvVmxLaunchOnVp(vmx::vm_state* VpData)
{
	//
	// Initialize all the VMX-related MSRs by reading their value
	//
	for (UINT32 i = 0; i < sizeof(VpData->launch_context.msr_data) / sizeof(VpData->launch_context.msr_data[0]); i++)
	{
		VpData->launch_context.msr_data[i].QuadPart = __readmsr(IA32_VMX_BASIC + i);
	}

	debug_log("[%d] mtrr init\n", thread::get_processor_index());

	//
	// Initialize all the MTRR-related MSRs by reading their value and build
	// range structures to describe their settings
	//
	ShvVmxMtrrInitialize(VpData);

	debug_log("[%d] ept init\n", thread::get_processor_index());

	//
	// Initialize the EPT structures
	//
	ShvVmxEptInitialize(VpData);

	debug_log("[%d] entering root mode\n", thread::get_processor_index());

	//
	// Attempt to enter VMX root mode on this processor.
	//
	if (ShvVmxEnterRootModeOnVp(VpData) == FALSE)
	{
		throw std::runtime_error("Not available");
	}

	debug_log("[%d] setting up vmcs\n", thread::get_processor_index());

	//
	// Initialize the VMCS, both guest and host state.
	//
	ShvVmxSetupVmcsForVp(VpData);

	//
	// Launch the VMCS, based on the guest data that was loaded into the
	// various VMCS fields by ShvVmxSetupVmcsForVp. This will cause the
	// processor to jump to ShvVpRestoreAfterLaunch on success, or return
	// back to the caller on failure.
	//
	debug_log("[%d] vmx launch\n", thread::get_processor_index());
	return ShvVmxLaunch();
}


void hypervisor::enable_core(const uint64_t system_directory_table_base)
{
	debug_log("[%d] Enabling hypervisor on core %d\n", thread::get_processor_index(), thread::get_processor_index());
	auto* vm_state = this->get_current_vm_state();

	vm_state->launch_context.system_directory_table_base = system_directory_table_base;

	debug_log("[%d] Capturing registers\n", thread::get_processor_index());
	ShvCaptureSpecialRegisters(&vm_state->launch_context.special_registers);

	//
	// Then, capture the entire register state. We will need this, as once we
	// launch the VM, it will begin execution at the defined guest instruction
	// pointer, which we set to ShvVpRestoreAfterLaunch, with the registers set
	// to whatever value they were deep inside the VMCS/VMX initialization code.
	// By using RtlRestoreContext, that function sets the AC flag in EFLAGS and
	// returns here with our registers restored.
	//
	debug_log("[%d] Capturing context\n", thread::get_processor_index());
	RtlCaptureContext(&vm_state->launch_context.context_frame);
	if ((__readeflags() & EFLAGS_ALIGNMENT_CHECK_FLAG_FLAG) == 0)
	{
		//
		// If the AC bit is not set in EFLAGS, it means that we have not yet
		// launched the VM. Attempt to initialize VMX on this processor.
		//
		debug_log("[%d] Launching\n", thread::get_processor_index());
		ShvVmxLaunchOnVp(vm_state);
	}

	if (!is_hypervisor_present())
	{
		throw std::runtime_error("Hypervisor is not present");
	}
}

void hypervisor::disable_core()
{
	int32_t cpu_info[4]{0};
	__cpuidex(cpu_info, 0x41414141, 0x42424242);
}

void hypervisor::allocate_vm_states()
{
	if (this->vm_states_)
	{
		throw std::runtime_error("VM states are still in use");
	}

	// As Windows technically supports cpu hot-plugging, keep track of the allocation count
	this->vm_state_count_ = thread::get_processor_count();
	this->vm_states_ = new vmx::vm_state*[this->vm_state_count_]{};
	if (!this->vm_states_)
	{
		throw std::runtime_error("Failed to allocate VM states array");
	}

	for (auto i = 0u; i < this->vm_state_count_; ++i)
	{
		this->vm_states_[i] = memory::allocate_aligned_object<vmx::vm_state>();
		if (!this->vm_states_[i])
		{
			throw std::runtime_error("Failed to allocate VM state entries");
		}
	}
}

void hypervisor::free_vm_states()
{
	for (auto i = 0u; i < this->vm_state_count_ && this->vm_states_; ++i)
	{
		memory::free_aligned_memory(this->vm_states_[i]);
	}

	delete[] this->vm_states_;
	this->vm_states_ = nullptr;
	this->vm_state_count_ = 0;
}

vmx::vm_state* hypervisor::get_current_vm_state() const
{
	const auto current_core = thread::get_processor_index();
	if (current_core >= this->vm_state_count_)
	{
		return nullptr;
	}

	return this->vm_states_[current_core];
}
