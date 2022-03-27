#include "std_include.hpp"
#include "hypervisor.hpp"

#include "exception.hpp"
#include "logging.hpp"
#include "finally.hpp"
#include "memory.hpp"
#include "thread.hpp"

#include <ia32.hpp>

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
		if ((data.cpuid_feature_information_ecx.flags & 0x80000000) == 0)
		{
			return false;
		}

		int32_t cpuid_data[4] = {0};
		__cpuid(cpuid_data, 0x40000001);
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

	bool success = true;
	thread::dispatch_on_all_cores([&]()
	{
		success &= this->try_enable_core(cr3);
	});

	if (!success)
	{
		this->disable();
		//throw std::runtime_error("Hypervisor initialization failed");
	}
}

bool hypervisor::try_enable_core(const uint64_t cr3)
{
	try
	{
		this->enable_core(cr3);
		return true;
	}
	catch (std::exception& e)
	{
		debug_log("Failed to enable hypervisor on core %d: %s\n", thread::get_processor_index(), e.what());
		return false;
	}catch (...)
	{
		debug_log("Failed to enable hypervisor on core %d.\n", thread::get_processor_index());
		return false;
	}
}

void hypervisor::enable_core(uint64_t /*cr3*/)
{
	auto* vm_state = this->get_current_vm_state();

	if (!is_hypervisor_present())
	{
		throw std::runtime_error("Hypervisor is not present");
	}
}

void hypervisor::disable_core()
{
	if (!is_hypervisor_present())
	{
		return;
	}

	auto* vm_state = this->get_current_vm_state();
}

void hypervisor::allocate_vm_states()
{
	if (this->vm_states_)
	{
		throw std::runtime_error("VM states are still in use");
	}

	const auto core_count = thread::get_processor_count();
	const auto allocation_size = sizeof(vmx::vm_state) * core_count;

	this->vm_states_ = static_cast<vmx::vm_state*>(memory::allocate_aligned_memory(allocation_size));
	if (!this->vm_states_)
	{
		throw std::runtime_error("Failed to allocate VM states");
	}

	RtlSecureZeroMemory(this->vm_states_, allocation_size);
}

void hypervisor::free_vm_states()
{
	memory::free_aligned_memory(this->vm_states_);
	this->vm_states_ = nullptr;
}

vmx::vm_state* hypervisor::get_current_vm_state() const
{
	const auto current_core = thread::get_processor_index();
	return &this->vm_states_[current_core];
}
