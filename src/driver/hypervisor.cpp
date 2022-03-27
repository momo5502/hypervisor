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
}

void hypervisor::enable()
{
	thread::dispatch_on_all_cores([this]()
	{
		this->enable_core();
	});
}

void hypervisor::enable_core()
{
}

void hypervisor::disable_core()
{
}
