#pragma once

#include "vmx.hpp"

class hypervisor
{
public:
	hypervisor();
	~hypervisor();

	hypervisor(hypervisor&& obj) noexcept = delete;
	hypervisor& operator=(hypervisor&& obj) noexcept = delete;

	hypervisor(const hypervisor& obj) = delete;
	hypervisor& operator=(const hypervisor& obj) = delete;

	void enable();
	void disable();

private:
	vmx::vm_state* vm_states_{nullptr};

	void enable_core(uint64_t system_directory_table_base);
	bool try_enable_core(uint64_t system_directory_table_base);
	void disable_core();

	void allocate_vm_states();
	void free_vm_states();

	vmx::vm_state* get_current_vm_state() const;
};
