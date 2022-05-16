#include "std_include.hpp"
#include "hypervisor.hpp"

#include "exception.hpp"
#include "logging.hpp"
#include "finally.hpp"
#include "memory.hpp"
#include "thread.hpp"
#include "assembly.hpp"
#include "string.hpp"

#define DPL_USER   3
#define DPL_SYSTEM 0

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

	// This absolutely needs to be inlined. Otherwise the stack might be broken upon restoration
	// See: https://github.com/ionescu007/SimpleVisor/issues/48
#define capture_cpu_context(launch_context) \
	      cpature_special_registers((launch_context).special_registers);\
	      RtlCaptureContext(&(launch_context).context_frame);

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
		uintptr_t data{};
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
		vm_state->launch_context.launched = true;
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

bool hypervisor::install_ept_hook(const void* destination, const void* source, const size_t length,
                                  vmx::ept_translation_hint* translation_hint)
{
	try
	{
		this->ept_->install_hook(destination, source, length, translation_hint);
	}
	catch (std::exception& e)
	{
		debug_log("Failed to install ept hook on core %d: %s\n", thread::get_processor_index(), e.what());
		return false;
	}
	catch (...)
	{
		debug_log("Failed to install ept hook on core %d.\n", thread::get_processor_index());
		return false;
	}

	volatile long failures = 0;
	thread::dispatch_on_all_cores([&]
	{
		if (!this->try_install_ept_hook_on_core(destination, source, length, translation_hint))
		{
			InterlockedIncrement(&failures);
		}
	});

	return failures == 0;
}

bool hypervisor::install_ept_code_watch_point(const uint64_t physical_page, bool invalidate) const
{
	try
	{
		this->ept_->install_code_watch_point(physical_page);
	}
	catch (std::exception& e)
	{
		debug_log("Failed to install ept watch point on core %d: %s\n", thread::get_processor_index(), e.what());
		return false;
	}
	catch (...)
	{
		debug_log("Failed to install ept watch point on core %d.\n", thread::get_processor_index());
		return false;
	}

	if (invalidate)
	{
		thread::dispatch_on_all_cores([&]
		{
			this->ept_->invalidate();
		});
	}

	return true;
}

bool hypervisor::install_ept_code_watch_points(const uint64_t* physical_pages, const size_t count) const
{
	bool success = true;
	for (size_t i = 0; i < count; ++i)
	{
		success &= this->install_ept_code_watch_point(physical_pages[i], false);
	}

	thread::dispatch_on_all_cores([&]
	{
		this->ept_->invalidate();
	});

	return success;
}

void hypervisor::disable_all_ept_hooks() const
{
	this->ept_->disable_all_hooks();

	thread::dispatch_on_all_cores([&]
	{
		auto* vm_state = this->get_current_vm_state();
		if (!vm_state)
		{
			return;
		}

		if (this->is_enabled())
		{
			vm_state->ept->invalidate();
		}
	});
}

vmx::ept& hypervisor::get_ept() const
{
	return *this->ept_;
}

hypervisor* hypervisor::get_instance()
{
	return instance;
}

void hypervisor::enable()
{
	const auto cr3 = __readcr3();

	this->ept_->initialize();

	volatile long failures = 0;
	thread::dispatch_on_all_cores([&]
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

void enter_root_mode_on_cpu(vmx::state& vm_state)
{
	auto* launch_context = &vm_state.launch_context;
	auto* registers = &launch_context->special_registers;

	ia32_vmx_basic_register basic_register{};
	memset(&basic_register, 0, sizeof(basic_register));

	basic_register.flags = launch_context->msr_data[0].QuadPart;
	if (basic_register.vmcs_size_in_bytes > static_cast<uint64_t>(PAGE_SIZE))
	{
		throw std::runtime_error("VMCS exceeds page size");
	}

	if (basic_register.memory_type != static_cast<uint64_t>(MEMORY_TYPE_WRITE_BACK))
	{
		throw std::runtime_error("VMCS memory type must be write-back");
	}

	if (basic_register.must_be_zero)
	{
		throw std::runtime_error("Must-be-zero bit is not zero :O");
	}

	ia32_vmx_ept_vpid_cap_register ept_vpid_cap_register{};
	ept_vpid_cap_register.flags = launch_context->msr_data[12].QuadPart;

	if (ept_vpid_cap_register.page_walk_length_4 &&
		ept_vpid_cap_register.memory_type_write_back &&
		ept_vpid_cap_register.pde_2mb_pages)
	{
		launch_context->ept_controls.flags = 0;
		launch_context->ept_controls.enable_ept = 1;
		launch_context->ept_controls.enable_vpid = 1;
	}

	vm_state.vmx_on.revision_id = launch_context->msr_data[0].LowPart;
	vm_state.vmcs.revision_id = launch_context->msr_data[0].LowPart;

	launch_context->vmx_on_physical_address = memory::get_physical_address(&vm_state.vmx_on);
	launch_context->vmcs_physical_address = memory::get_physical_address(&vm_state.vmcs);
	launch_context->msr_bitmap_physical_address = memory::get_physical_address(vm_state.msr_bitmap);

	registers->cr0 &= launch_context->msr_data[7].LowPart;
	registers->cr0 |= launch_context->msr_data[6].LowPart;

	registers->cr4 &= launch_context->msr_data[9].LowPart;
	registers->cr4 |= launch_context->msr_data[8].LowPart;

	__writecr0(registers->cr0);
	__writecr4(registers->cr4);

	if (__vmx_on(&launch_context->vmx_on_physical_address))
	{
		throw std::runtime_error("Failed to execute vmx_on");
	}

	auto destructor = utils::finally([]
	{
		__vmx_off();
	});

	if (__vmx_vmclear(&launch_context->vmcs_physical_address))
	{
		throw std::runtime_error("Failed to clear vmcs");
	}

	if (__vmx_vmptrld(&launch_context->vmcs_physical_address))
	{
		throw std::runtime_error("Failed to load vmcs");
	}

	destructor.cancel();
}

vmx::gdt_entry convert_gdt_entry(const uint64_t gdt_base, const uint16_t selector_value)
{
	vmx::gdt_entry result{};
	memset(&result, 0, sizeof(result));

	segment_selector selector{};
	selector.flags = selector_value;

	if (selector.flags == 0 || selector.table)
	{
		result.limit = 0;
		result.access_rights.flags = 0;
		result.base = 0;
		result.selector.flags = 0;
		result.access_rights.unusable = 1;
		return result;
	}

	const auto* gdt_entry = reinterpret_cast<segment_descriptor_64*>(gdt_base + static_cast<uint64_t>(selector.index) *
		8);

	result.selector = selector;
	result.limit = __segmentlimit(selector.flags);

	result.base = 0;
	result.base |= static_cast<uint64_t>(gdt_entry->base_address_low);
	result.base |= static_cast<uint64_t>(gdt_entry->base_address_middle) << 16;
	result.base |= static_cast<uint64_t>(gdt_entry->base_address_high) << 24;
	if (gdt_entry->descriptor_type == 0u)
	{
		result.base |= static_cast<uint64_t>(gdt_entry->base_address_upper) << 32;
	}

	result.access_rights.flags = 0;

	result.access_rights.type = gdt_entry->type;
	result.access_rights.descriptor_type = gdt_entry->descriptor_type;
	result.access_rights.descriptor_privilege_level = gdt_entry->descriptor_privilege_level;
	result.access_rights.present = gdt_entry->present;
	result.access_rights.reserved1 = gdt_entry->segment_limit_high;
	result.access_rights.available_bit = gdt_entry->system;
	result.access_rights.long_mode = gdt_entry->long_mode;
	result.access_rights.default_big = gdt_entry->default_big;
	result.access_rights.granularity = gdt_entry->granularity;

	result.access_rights.reserved1 = 0;
	result.access_rights.unusable = !gdt_entry->present;

	return result;
}

uint32_t adjust_msr(const ULARGE_INTEGER control_value, const uint64_t desired_value)
{
	auto result = static_cast<uint32_t>(desired_value);
	result &= control_value.HighPart;
	result |= control_value.LowPart;
	return result;
}

void vmx_handle_invd()
{
	__wbinvd();
}

void vmx_handle_cpuid(vmx::guest_context& guest_context)
{
	INT32 cpu_info[4];

	if (guest_context.vp_regs->Rax == 0x41414141 &&
		guest_context.vp_regs->Rcx == 0x42424242 &&
		(read_vmx(VMCS_GUEST_CS_SELECTOR) & SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK) == DPL_SYSTEM)
	{
		guest_context.exit_vm = true;
		return;
	}

	__cpuidex(cpu_info, static_cast<int32_t>(guest_context.vp_regs->Rax),
	          static_cast<int32_t>(guest_context.vp_regs->Rcx));

	if (guest_context.vp_regs->Rax == 1)
	{
		cpu_info[2] |= HYPERV_HYPERVISOR_PRESENT_BIT;
	}
	else if (guest_context.vp_regs->Rax == HYPERV_CPUID_INTERFACE)
	{
		cpu_info[0] = 'momo';
	}

	guest_context.vp_regs->Rax = cpu_info[0];
	guest_context.vp_regs->Rbx = cpu_info[1];
	guest_context.vp_regs->Rcx = cpu_info[2];
	guest_context.vp_regs->Rdx = cpu_info[3];
}

void vmx_handle_xsetbv(const vmx::guest_context& guest_context)
{
	_xsetbv(static_cast<uint32_t>(guest_context.vp_regs->Rcx),
	        guest_context.vp_regs->Rdx << 32 | guest_context.vp_regs->Rax);
}

void vmx_handle_vmx(vmx::guest_context& guest_context)
{
	guest_context.guest_e_flags |= 0x1; // VM_FAIL_INVALID
	__vmx_vmwrite(VMCS_GUEST_RFLAGS, guest_context.guest_e_flags);
}

void vmx_dispatch_vm_exit(vmx::guest_context& guest_context, const vmx::state& vm_state)
{
	switch (guest_context.exit_reason)
	{
	case VMX_EXIT_REASON_EXECUTE_CPUID:
		vmx_handle_cpuid(guest_context);
		break;
	case VMX_EXIT_REASON_EXECUTE_INVD:
		vmx_handle_invd();
		break;
	case VMX_EXIT_REASON_EXECUTE_XSETBV:
		vmx_handle_xsetbv(guest_context);
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
		vmx_handle_vmx(guest_context);
		break;
	case VMX_EXIT_REASON_EPT_VIOLATION:
		vm_state.ept->handle_violation(guest_context);
		break;
	case VMX_EXIT_REASON_EPT_MISCONFIGURATION:
		vm_state.ept->handle_misconfiguration(guest_context);
		break;
	//case VMX_EXIT_REASON_EXECUTE_RDTSC:
	//	break;
	default:
		break;
	}

	if (guest_context.increment_rip)
	{
		guest_context.guest_rip += read_vmx(VMCS_VMEXIT_INSTRUCTION_LENGTH);
		__vmx_vmwrite(VMCS_GUEST_RIP, guest_context.guest_rip);
	}
}

extern "C" [[ noreturn ]] void vm_exit_handler(CONTEXT* context)
{
	auto* vm_state = resolve_vm_state_from_context(*context);

	vmx::guest_context guest_context{};
	guest_context.guest_e_flags = read_vmx(VMCS_GUEST_RFLAGS);
	guest_context.guest_rip = read_vmx(VMCS_GUEST_RIP);
	guest_context.guest_rsp = read_vmx(VMCS_GUEST_RSP);
	guest_context.guest_physical_address = read_vmx(VMCS_GUEST_PHYSICAL_ADDRESS);
	guest_context.exit_reason = read_vmx(VMCS_EXIT_REASON) & 0xFFFF;
	guest_context.exit_qualification = read_vmx(VMCS_EXIT_QUALIFICATION);
	guest_context.vp_regs = context;
	guest_context.exit_vm = false;
	guest_context.increment_rip = true;

	vmx_dispatch_vm_exit(guest_context, *vm_state);

	if (guest_context.exit_vm)
	{
		context->Rcx = 0x43434343;
		context->Rsp = guest_context.guest_rsp;
		context->Rip = guest_context.guest_rip;
		context->EFlags = static_cast<uint32_t>(guest_context.guest_e_flags);

		restore_descriptor_tables(vm_state->launch_context);

		__writecr3(read_vmx(VMCS_GUEST_CR3));
		__vmx_off();
	}
	else
	{
		context->Rip = reinterpret_cast<uint64_t>(resume_vmx);
	}

	restore_context(context);
}

void setup_vmcs_for_cpu(vmx::state& vm_state)
{
	auto* launch_context = &vm_state.launch_context;
	auto* state = &launch_context->special_registers;
	auto* context = &launch_context->context_frame;

	__vmx_vmwrite(VMCS_GUEST_VMCS_LINK_POINTER, ~0ULL);

	if (launch_context->ept_controls.flags != 0)
	{
		const auto vmx_eptp = vm_state.ept->get_ept_pointer();
		__vmx_vmwrite(VMCS_CTRL_EPT_POINTER, vmx_eptp.flags);
		__vmx_vmwrite(VMCS_CTRL_VIRTUAL_PROCESSOR_IDENTIFIER, 1);
	}

	__vmx_vmwrite(VMCS_CTRL_MSR_BITMAP_ADDRESS, launch_context->msr_bitmap_physical_address);

	auto ept_controls = launch_context->ept_controls;
	ept_controls.enable_rdtscp = 1;
	ept_controls.enable_invpcid = 1;
	ept_controls.enable_xsaves = 1;
	__vmx_vmwrite(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
	              adjust_msr(launch_context->msr_data[11], ept_controls.flags));

	__vmx_vmwrite(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, adjust_msr(launch_context->msr_data[13], 0));

	ia32_vmx_procbased_ctls_register procbased_ctls_register{};
	procbased_ctls_register.activate_secondary_controls = 1;
	procbased_ctls_register.use_msr_bitmaps = 1;

	__vmx_vmwrite(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
	              adjust_msr(launch_context->msr_data[14],
	                         procbased_ctls_register.flags));

	ia32_vmx_exit_ctls_register exit_ctls_register{};
	exit_ctls_register.host_address_space_size = 1;
	__vmx_vmwrite(VMCS_CTRL_VMEXIT_CONTROLS,
	              adjust_msr(launch_context->msr_data[15],
	                         exit_ctls_register.flags));

	ia32_vmx_entry_ctls_register entry_ctls_register{};
	entry_ctls_register.ia32e_mode_guest = 1;
	__vmx_vmwrite(VMCS_CTRL_VMENTRY_CONTROLS,
	              adjust_msr(launch_context->msr_data[16],
	                         entry_ctls_register.flags));

	vmx::gdt_entry gdt_entry{};
	gdt_entry = convert_gdt_entry(state->gdtr.base_address, context->SegCs);
	__vmx_vmwrite(VMCS_GUEST_CS_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_CS_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_CS_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_CS_SELECTOR, context->SegCs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	gdt_entry = convert_gdt_entry(state->gdtr.base_address, context->SegSs);
	__vmx_vmwrite(VMCS_GUEST_SS_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_SS_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_SS_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_SS_SELECTOR, context->SegSs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	gdt_entry = convert_gdt_entry(state->gdtr.base_address, context->SegDs);
	__vmx_vmwrite(VMCS_GUEST_DS_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_DS_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_DS_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_DS_SELECTOR, context->SegDs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	gdt_entry = convert_gdt_entry(state->gdtr.base_address, context->SegEs);
	__vmx_vmwrite(VMCS_GUEST_ES_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_ES_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_ES_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_ES_SELECTOR, context->SegEs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	gdt_entry = convert_gdt_entry(state->gdtr.base_address, context->SegFs);
	__vmx_vmwrite(VMCS_GUEST_FS_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_FS_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_FS_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_FS_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_FS_SELECTOR, context->SegFs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	gdt_entry = convert_gdt_entry(state->gdtr.base_address, context->SegGs);
	__vmx_vmwrite(VMCS_GUEST_GS_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_GS_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_GS_BASE, state->msr_gs_base);
	__vmx_vmwrite(VMCS_HOST_GS_BASE, state->msr_gs_base);
	__vmx_vmwrite(VMCS_HOST_GS_SELECTOR, context->SegGs & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	gdt_entry = convert_gdt_entry(state->gdtr.base_address, state->tr);
	__vmx_vmwrite(VMCS_GUEST_TR_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_TR_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_TR_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_TR_BASE, gdt_entry.base);
	__vmx_vmwrite(VMCS_HOST_TR_SELECTOR, state->tr & ~SEGMENT_ACCESS_RIGHTS_DESCRIPTOR_PRIVILEGE_LEVEL_MASK);

	gdt_entry = convert_gdt_entry(state->gdtr.base_address, state->ldtr);
	__vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, gdt_entry.selector.flags);
	__vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, gdt_entry.limit);
	__vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, gdt_entry.access_rights.flags);
	__vmx_vmwrite(VMCS_GUEST_LDTR_BASE, gdt_entry.base);

	__vmx_vmwrite(VMCS_GUEST_GDTR_BASE, state->gdtr.base_address);
	__vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, state->gdtr.limit);
	__vmx_vmwrite(VMCS_HOST_GDTR_BASE, state->gdtr.base_address);

	__vmx_vmwrite(VMCS_GUEST_IDTR_BASE, state->idtr.base_address);
	__vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, state->idtr.limit);
	__vmx_vmwrite(VMCS_HOST_IDTR_BASE, state->idtr.base_address);

	__vmx_vmwrite(VMCS_CTRL_CR0_READ_SHADOW, state->cr0);
	__vmx_vmwrite(VMCS_HOST_CR0, state->cr0);
	__vmx_vmwrite(VMCS_GUEST_CR0, state->cr0);

	__vmx_vmwrite(VMCS_HOST_CR3, launch_context->system_directory_table_base);
	__vmx_vmwrite(VMCS_GUEST_CR3, state->cr3);

	__vmx_vmwrite(VMCS_HOST_CR4, state->cr4);
	__vmx_vmwrite(VMCS_GUEST_CR4, state->cr4);
	__vmx_vmwrite(VMCS_CTRL_CR4_READ_SHADOW, state->cr4);

	__vmx_vmwrite(VMCS_GUEST_DEBUGCTL, state->debug_control);
	__vmx_vmwrite(VMCS_GUEST_DR7, state->kernel_dr7);

	const auto stack_pointer = reinterpret_cast<uintptr_t>(vm_state.stack_buffer) + KERNEL_STACK_SIZE - sizeof(CONTEXT);

	__vmx_vmwrite(VMCS_GUEST_RSP, stack_pointer);
	__vmx_vmwrite(VMCS_GUEST_RIP, reinterpret_cast<uintptr_t>(vm_launch));
	__vmx_vmwrite(VMCS_GUEST_RFLAGS, context->EFlags);

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

[[ noreturn ]] void launch_hypervisor(vmx::state& vm_state)
{
	initialize_msrs(vm_state.launch_context);
	//vm_state.ept->initialize();

	enter_root_mode_on_cpu(vm_state);
	setup_vmcs_for_cpu(vm_state);

	auto error_code = launch_vmx();
	throw std::runtime_error(string::va("Failed to launch vmx: %X", error_code));
}


void hypervisor::enable_core(const uint64_t system_directory_table_base)
{
	debug_log("Enabling hypervisor on core %d\n", thread::get_processor_index());
	auto* vm_state = this->get_current_vm_state();

	if (!is_virtualization_supported())
	{
		throw std::runtime_error("VMX not supported on this core");
	}

	vm_state->launch_context.launched = false;
	vm_state->launch_context.system_directory_table_base = system_directory_table_base;

	// Must be inlined here, otherwise the stack is broken
	capture_cpu_context(vm_state->launch_context);

	if (!vm_state->launch_context.launched)
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
	if (!this->ept_)
	{
		this->ept_ = memory::allocate_aligned_object<vmx::ept>();
		if (!this->ept_)
		{
			throw std::runtime_error("Failed to allocate ept object");
		}
	}

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

		this->vm_states_[i]->ept = this->ept_;
	}
}

void hypervisor::free_vm_states()
{
	if (this->vm_states_)
	{
		for (auto i = 0u; i < this->vm_state_count_; ++i)
		{
			memory::free_aligned_object(this->vm_states_[i]);
		}

		delete[] this->vm_states_;
		this->vm_states_ = nullptr;
		this->vm_state_count_ = 0;
	}

	if (this->ept_)
	{
		memory::free_aligned_object(this->ept_);
		this->ept_ = nullptr;
	}
}

bool hypervisor::try_install_ept_hook_on_core(const void* destination, const void* source, const size_t length,
                                              vmx::ept_translation_hint* translation_hint)
{
	try
	{
		this->install_ept_hook_on_core(destination, source, length, translation_hint);
		return true;
	}
	catch (std::exception& e)
	{
		debug_log("Failed to install ept hook on core %d: %s\n", thread::get_processor_index(), e.what());
		return false;
	}
	catch (...)
	{
		debug_log("Failed to install ept hook on core %d.\n", thread::get_processor_index());
		return false;
	}
}

void hypervisor::install_ept_hook_on_core(const void* destination, const void* source, const size_t length,
                                          vmx::ept_translation_hint* translation_hint)
{
	auto* vm_state = this->get_current_vm_state();
	if (!vm_state)
	{
		throw std::runtime_error("No vm state available");
	}

	(void)destination;
	(void)source;
	(void)length;
	(void)translation_hint;
	//vm_state->ept->install_hook(destination, source, length, translation_hint);

	if (this->is_enabled())
	{
		vm_state->ept->invalidate();
	}
}

vmx::state* hypervisor::get_current_vm_state() const
{
	const auto current_core = thread::get_processor_index();
	if (!this->vm_states_ || current_core >= this->vm_state_count_)
	{
		return nullptr;
	}

	return this->vm_states_[current_core];
}
