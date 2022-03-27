#include "std_include.hpp"
#include "hypervisor.hpp"

#include "exception.hpp"
#include "logging.hpp"
#include "finally.hpp"
#include "thread.hpp"

#define IA32_FEATURE_CONTROL_MSR 0x3A
#define IA32_FEATURE_CONTROL_MSR_LOCK 0x0001
#define IA32_FEATURE_CONTROL_MSR_ENABLE_VMXON_OUTSIDE_SMX 0x0004

namespace
{
	hypervisor* instance{nullptr};

	bool is_vmx_supported()
	{
		int cpuid_data[4] = {0};
		__cpuid(cpuid_data, 1);
		return cpuid_data[2] & 0x20;
	}

	bool is_vmx_available()
	{
		const auto feature_control = __readmsr(IA32_FEATURE_CONTROL_MSR);
		const auto is_locked = (feature_control & IA32_FEATURE_CONTROL_MSR_LOCK) != 0;
		const auto is_enabled_outside_smx = (feature_control & IA32_FEATURE_CONTROL_MSR_ENABLE_VMXON_OUTSIDE_SMX) != 0;

		return is_locked && is_enabled_outside_smx;
	}

	bool is_virtualization_supported()
	{
		return is_vmx_supported() && is_vmx_available();
	}

	_IRQL_requires_max_(DISPATCH_LEVEL)

	void free_aligned_memory(void* memory)
	{
		MmFreeContiguousMemory(memory);
	}

	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)

	void* allocate_aligned_memory(const size_t size)
	{
		PHYSICAL_ADDRESS lowest{}, highest{};
		lowest.QuadPart = 0;
		highest.QuadPart = lowest.QuadPart - 1;

#if (NTDDI_VERSION >= NTDDI_VISTA)
		return MmAllocateContiguousNodeMemory(size,
		                                      lowest,
		                                      highest,
		                                      lowest,
		                                      PAGE_READWRITE,
		                                      KeGetCurrentNodeNumber());
#else
		return MmAllocateContiguousMemory(size, highest);
#endif
	}
}

hypervisor::hypervisor()
{
	if (instance != nullptr)
	{
		throw std::runtime_error("Hypervisor already instantiated");
	}

	auto destructor = utils::finally([]()
	{
		instance = nullptr;
	});
	instance = this;

	if (!is_virtualization_supported())
	{
		throw std::runtime_error("VMX not supported on this machine");
	}

	debug_log("VMX supported!\n");
	this->enable();
	destructor.cancel();
}

hypervisor::~hypervisor()
{
	this->disable();
	instance = nullptr;
}

void hypervisor::disable()
{
	thread::dispatch_on_all_cores([this]()
	{
		this->disable_core();
	});

	this->free_vm_states();
}

void hypervisor::enable()
{
	this->allocate_vm_states();

	thread::dispatch_on_all_cores([this]()
	{
		this->enable_core();
	});
}

void hypervisor::enable_core()
{
	auto* vm_state = this->get_current_vm_state();
}

void hypervisor::disable_core()
{
	auto* vm_state = this->get_current_vm_state();
}

void hypervisor::allocate_vm_states()
{
	const auto core_count = thread::get_processor_count();
	const auto allocation_size = sizeof(vmx::vm_state) * core_count;

	this->vm_states_ = static_cast<vmx::vm_state*>(allocate_aligned_memory(allocation_size));
	if(!this->vm_states_)
	{
		throw std::runtime_error("Failed to allocate vm states");
	}
}

void hypervisor::free_vm_states()
{
	if(this->vm_states_)
	{
		free_aligned_memory(this->vm_states_);
		this->vm_states_ = nullptr;
	}
}

vmx::vm_state* hypervisor::get_current_vm_state() const
{
	const auto current_core = thread::get_processor_index();
	return &this->vm_states_[current_core];
}
