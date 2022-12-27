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

	bool is_enabled() const;

	bool install_ept_hook(const void* destination, const void* source, size_t length, process_id source_pid,
	                      process_id target_pid, const utils::list<vmx::ept_translation_hint>& hints = {});

	bool install_ept_code_watch_point(uint64_t physical_page, process_id source_pid, process_id target_pid,
	                                  bool invalidate = true) const;
	bool install_ept_code_watch_points(const uint64_t* physical_pages, size_t count, process_id source_pid,
	                                   process_id target_pid) const;

	void disable_all_ept_hooks() const;

	vmx::ept& get_ept() const;

	static hypervisor* get_instance();

	void handle_process_termination(process_id process);

private:
	uint32_t vm_state_count_{0};
	vmx::state** vm_states_{nullptr};
	vmx::ept* ept_{nullptr};

	void enable_core(uint64_t system_directory_table_base);
	bool try_enable_core(uint64_t system_directory_table_base);
	void disable_core();

	void allocate_vm_states();
	void free_vm_states();

	void invalidate_cores() const;

	vmx::state* get_current_vm_state() const;
};
