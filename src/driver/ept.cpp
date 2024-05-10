#include "std_include.hpp"
#include "ept.hpp"

#include "assembly.hpp"
#include "finally.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "vmx.hpp"

namespace vmx
{
	namespace
	{
		struct mtrr_range
		{
			uint32_t enabled;
			uint32_t type;
			uint64_t physical_address_min;
			uint64_t physical_address_max;
		};

		using mtrr_list = mtrr_range[16];

		void initialize_mtrr(mtrr_list& mtrr_data)
		{
			ia32_mtrr_capabilities_register mtrr_capabilities{};
			mtrr_capabilities.flags = __readmsr(IA32_MTRR_CAPABILITIES);

			for (auto i = 0u; i < mtrr_capabilities.variable_range_count; i++)
			{
				ia32_mtrr_physbase_register mtrr_base{};
				ia32_mtrr_physmask_register mtrr_mask{};

				mtrr_base.flags = __readmsr(IA32_MTRR_PHYSBASE0 + i * 2);
				mtrr_mask.flags = __readmsr(IA32_MTRR_PHYSMASK0 + i * 2);

				mtrr_data[i].type = static_cast<uint32_t>(mtrr_base.type);
				mtrr_data[i].enabled = static_cast<uint32_t>(mtrr_mask.valid);
				if (mtrr_data[i].enabled != FALSE)
				{
					mtrr_data[i].physical_address_min = mtrr_base.page_frame_number *
						MTRR_PAGE_SIZE;

					unsigned long bit{};
					_BitScanForward64(&bit, mtrr_mask.page_frame_number * MTRR_PAGE_SIZE);
					mtrr_data[i].physical_address_max = mtrr_data[i].
						physical_address_min +
						(1ULL << bit) - 1;
				}
			}
		}

		uint32_t mtrr_adjust_effective_memory_type(const mtrr_list& mtrr_data, const uint64_t large_page_address,
		                                           uint32_t candidate_memory_type)
		{
			for (const auto& mtrr_entry : mtrr_data)
			{
				if (mtrr_entry.enabled && large_page_address + (2_mb - 1) >= mtrr_entry.physical_address_min &&
					large_page_address <= mtrr_entry.physical_address_max)
				{
					candidate_memory_type = mtrr_entry.type;
				}
			}

			return candidate_memory_type;
		}

		void update_fake_page(ept_hook& hook)
		{
			if (!hook.mapped_virtual_address)
			{
				return;
			}

			uint8_t page_copy[PAGE_SIZE];
			memcpy(page_copy, hook.mapped_virtual_address, PAGE_SIZE);

			for (size_t i = 0; i < PAGE_SIZE; ++i)
			{
				if (hook.diff_page[i] != page_copy[i])
				{
					hook.diff_page[i] = page_copy[i];
					hook.fake_page[i] = page_copy[i];
				}
			}
		}

		void reset_all_watch_point_pages(utils::list<ept_code_watch_point>& watch_points)
		{
			for (const auto& watch_point : watch_points)
			{
				if (watch_point.target_page)
				{
					watch_point.target_page->write_access = 0;
					watch_point.target_page->read_access = 0;
					watch_point.target_page->execute_access = 1;
				}
			}
		}
	}

	ept_hook::ept_hook(const uint64_t physical_base)
		: physical_base_address(physical_base)
		  , mapped_virtual_address(memory::map_physical_memory(physical_base_address, PAGE_SIZE))
	{
		if (!mapped_virtual_address)
		{
			throw std::runtime_error("Failed to map physical memory");
		}
	}

	ept_hook::~ept_hook()
	{
		this->target_page->flags = this->original_entry.flags;

		if (mapped_virtual_address)
		{
			memory::unmap_physical_memory(mapped_virtual_address, PAGE_SIZE);
		}
	}

	ept::ept()
	{
		// Directly initializing these fields kills the compiler ._.
		// https://developercommunity.visualstudio.com/t/clexe-using-20gb-of-memory-compiling-small-file-in/407999
		memset(this->epml4, 0, sizeof(this->epml4));
		memset(this->epdpt, 0, sizeof(this->epdpt));
		memset(this->epde, 0, sizeof(this->epde));

		memset(this->access_records, 0, sizeof(this->access_records));
	}

	ept::~ept()
	{
		this->disable_all_hooks();
	}

	void ept::install_page_hook(void* destination, const void* source, const size_t length, const process_id source_pid,
	                            const process_id target_pid,
	                            const ept_translation_hint* translation_hint)
	{
		auto* hook = this->get_or_create_ept_hook(destination, translation_hint);
		hook->source_pid = source_pid;
		hook->target_pid = target_pid;

		const auto page_offset = ADDRMASK_EPT_PML1_OFFSET(reinterpret_cast<uint64_t>(destination));
		memcpy(hook->fake_page + page_offset, source, length);
	}

	void ept::record_access(const uint64_t rip)
	{
		for (auto& access_record : this->access_records)
		{
			if (access_record == 0)
			{
				access_record = rip;
				return;
			}

			if (access_record == rip)
			{
				return;
			}
		}
	}

	void ept::install_hook(const void* destination, const void* source, const size_t length,
	                       const process_id source_pid, const process_id target_pid,
	                       const utils::list<ept_translation_hint>& hints)
	{
		auto current_destination = reinterpret_cast<uint64_t>(destination);
		auto current_source = reinterpret_cast<uint64_t>(source);
		auto current_length = length;

		while (current_length != 0)
		{
			const auto aligned_destination = PAGE_ALIGN(current_destination);
			const auto page_offset = ADDRMASK_EPT_PML1_OFFSET(current_destination);
			const auto page_remaining = PAGE_SIZE - page_offset;
			const auto data_to_write = min(page_remaining, current_length);


			const ept_translation_hint* relevant_hint = nullptr;
			for (const auto& hint : hints)
			{
				if (hint.virtual_base_address == aligned_destination)
				{
					relevant_hint = &hint;
					break;
				}
			}

			this->install_page_hook(reinterpret_cast<void*>(current_destination),
			                        reinterpret_cast<const void*>(current_source), data_to_write, source_pid,
			                        target_pid, relevant_hint);

			current_length -= data_to_write;
			current_destination += data_to_write;
			current_source += data_to_write;
		}
	}

	void ept::disable_all_hooks()
	{
		this->ept_hooks.clear();
	}

	void ept::handle_violation(guest_context& guest_context)
	{
		vmx_exit_qualification_ept_violation violation_qualification{};
		violation_qualification.flags = guest_context.exit_qualification;

		if (!violation_qualification.caused_by_translation)
		{
			guest_context.exit_vm = true;
		}

		const auto physical_base_address = reinterpret_cast<uint64_t>(PAGE_ALIGN(guest_context.guest_physical_address));

		// watch-point stuff

		auto* watch_point = this->find_ept_code_watch_point(physical_base_address);
		if (watch_point)
		{
			reset_all_watch_point_pages(this->ept_code_watch_points);

			if (!violation_qualification.ept_executable && violation_qualification.execute_access)
			{
				watch_point->target_page->execute_access = 1;
				watch_point->target_page->write_access = 0;
				watch_point->target_page->read_access = 0;
				guest_context.increment_rip = false;
			}
			else if (violation_qualification.ept_executable && (violation_qualification.read_access ||
				violation_qualification.
				write_access))
			{
				watch_point->target_page->execute_access = 0;
				watch_point->target_page->read_access = 1;
				watch_point->target_page->write_access = 1;
				guest_context.increment_rip = false;
				if (violation_qualification.read_access)
				{
					this->record_access(guest_context.guest_rip);
				}
			}

			return;
		}

		// ept-hooking stuff

		auto* hook = this->find_ept_hook(physical_base_address);
		if (!hook)
		{
			return;
		}

		if (!violation_qualification.ept_executable && violation_qualification.execute_access)
		{
			update_fake_page(*hook);
			hook->target_page->flags = hook->execute_entry.flags;
			guest_context.increment_rip = false;
		}

		if (violation_qualification.ept_executable && (violation_qualification.read_access || violation_qualification.
			write_access))
		{
			hook->target_page->flags = hook->readwrite_entry.flags;
			guest_context.increment_rip = false;
		}
	}

	void ept::handle_misconfiguration(guest_context& guest_context) const
	{
		// We can actually not recover from this, but this should not occur anyways
		guest_context.increment_rip = false;
		guest_context.exit_vm = true;
	}

	void ept::initialize()
	{
		mtrr_list mtrr_data{};
		initialize_mtrr(mtrr_data);

		this->epml4[0].read_access = 1;
		this->epml4[0].write_access = 1;
		this->epml4[0].execute_access = 1;
		this->epml4[0].page_frame_number = memory::get_physical_address(&this->epdpt) /
			PAGE_SIZE;

		// --------------------------

		epdpte temp_epdpte{};
		temp_epdpte.flags = 0;
		temp_epdpte.read_access = 1;
		temp_epdpte.write_access = 1;
		temp_epdpte.execute_access = 1;

		__stosq(reinterpret_cast<uint64_t*>(&this->epdpt[0]), temp_epdpte.flags, EPT_PDPTE_ENTRY_COUNT);

		for (auto i = 0; i < EPT_PDPTE_ENTRY_COUNT; i++)
		{
			this->epdpt[i].page_frame_number = memory::get_physical_address(&this->epde[i][0]) / PAGE_SIZE;
		}

		// --------------------------

		epde_2mb temp_epde{};
		temp_epde.flags = 0;
		temp_epde.read_access = 1;
		temp_epde.write_access = 1;
		temp_epde.execute_access = 1;
		temp_epde.large_page = 1;

		__stosq(reinterpret_cast<uint64_t*>(this->epde), temp_epde.flags, EPT_PDPTE_ENTRY_COUNT * EPT_PDE_ENTRY_COUNT);

		for (auto i = 0; i < EPT_PDPTE_ENTRY_COUNT; i++)
		{
			for (auto j = 0; j < EPT_PDE_ENTRY_COUNT; j++)
			{
				this->epde[i][j].page_frame_number = (i * 512) + j;
				this->epde[i][j].memory_type = mtrr_adjust_effective_memory_type(
					mtrr_data, this->epde[i][j].page_frame_number * 2_mb, MEMORY_TYPE_WRITE_BACK);
			}
		}
	}

	void ept::install_code_watch_point(const uint64_t physical_page, const process_id source_pid,
	                                   const process_id target_pid)
	{
		const auto physical_base_address = reinterpret_cast<uint64_t>(PAGE_ALIGN(physical_page));

		if (this->find_ept_code_watch_point(physical_base_address))
		{
			return;
		}

		auto& watch_point = this->allocate_ept_code_watch_point();
		watch_point.source_pid = source_pid;
		watch_point.target_pid = target_pid;

		this->split_large_page(physical_base_address);

		watch_point.physical_base_address = physical_base_address;
		watch_point.target_page = this->get_pml1_entry(physical_base_address);
		if (!watch_point.target_page)
		{
			throw std::runtime_error("Failed to get PML1 entry for target address");
		}

		watch_point.target_page->write_access = 0;
		watch_point.target_page->read_access = 0;
	}

	ept_pointer ept::get_ept_pointer() const
	{
		const auto ept_pml4_physical_address = memory::get_physical_address(const_cast<pml4*>(&this->epml4[0]));

		ept_pointer vmx_eptp{};
		vmx_eptp.flags = 0;
		vmx_eptp.page_walk_length = 3;
		vmx_eptp.memory_type = MEMORY_TYPE_WRITE_BACK;
		vmx_eptp.page_frame_number = ept_pml4_physical_address / PAGE_SIZE;

		return vmx_eptp;
	}

	void ept::invalidate() const
	{
		const auto ept_pointer = this->get_ept_pointer();

		invept_descriptor descriptor{};
		descriptor.ept_pointer = ept_pointer.flags;
		descriptor.reserved = 0;

		__invept(1, &descriptor);
	}

	pml2* ept::get_pml2_entry(const uint64_t physical_address)
	{
		const auto directory = ADDRMASK_EPT_PML2_INDEX(physical_address);
		const auto directory_pointer = ADDRMASK_EPT_PML3_INDEX(physical_address);
		const auto pml4_entry = ADDRMASK_EPT_PML4_INDEX(physical_address);

		if (pml4_entry > 0)
		{
			return nullptr;
		}

		return &this->epde[directory_pointer][directory];
	}

	pml1* ept::get_pml1_entry(const uint64_t physical_address)
	{
		auto* pml2_entry = this->get_pml2_entry(physical_address);
		if (!pml2_entry || pml2_entry->large_page)
		{
			return nullptr;
		}

		const auto* pml2 = reinterpret_cast<pml2_ptr*>(pml2_entry);
		auto* pml1 = this->find_pml1_table(pml2->page_frame_number * PAGE_SIZE);
		if (!pml1)
		{
			pml1 = static_cast<epte*>(memory::get_virtual_address(pml2->page_frame_number * PAGE_SIZE));
		}

		if (!pml1)
		{
			return nullptr;
		}

		return &pml1[ADDRMASK_EPT_PML1_INDEX(physical_address)];
	}

	pml1* ept::find_pml1_table(const uint64_t physical_address)
	{
		for (auto& split : this->ept_splits)
		{
			if (memory::get_physical_address(&split.pml1[0]) == physical_address)
			{
				return split.pml1;
			}
		}

		return nullptr;
	}

	ept_split& ept::allocate_ept_split()
	{
		return this->ept_splits.emplace_back();
	}

	ept_hook& ept::allocate_ept_hook(const uint64_t physical_address)
	{
		return this->ept_hooks.emplace_back(physical_address);
	}

	ept_hook* ept::find_ept_hook(const uint64_t physical_address)
	{
		for (auto& hook : this->ept_hooks)
		{
			if (hook.physical_base_address == physical_address)
			{
				return &hook;
			}
		}

		return nullptr;
	}

	ept_code_watch_point& ept::allocate_ept_code_watch_point()
	{
		return this->ept_code_watch_points.emplace_back();
	}

	ept_code_watch_point* ept::find_ept_code_watch_point(const uint64_t physical_address)
	{
		for (auto& watch_point : this->ept_code_watch_points)
		{
			if (watch_point.physical_base_address == physical_address)
			{
				return &watch_point;
			}
		}

		return nullptr;
	}

	ept_hook* ept::get_or_create_ept_hook(void* destination, const ept_translation_hint* translation_hint)
	{
		const auto virtual_target = PAGE_ALIGN(destination);

		uint64_t physical_address = 0;
		if (translation_hint)
		{
			physical_address = translation_hint->physical_base_address + ADDRMASK_EPT_PML1_OFFSET(
				reinterpret_cast<uint64_t>(destination));
		}
		else
		{
			physical_address = memory::get_physical_address(virtual_target);
		}

		if (!physical_address)
		{
			throw std::runtime_error("No physical address for destination");
		}

		const auto physical_base_address = reinterpret_cast<uint64_t>(PAGE_ALIGN(physical_address));
		auto* hook = this->find_ept_hook(physical_base_address);
		if (hook)
		{
			if (hook->target_page->flags == hook->original_entry.flags)
			{
				const auto* data_source = translation_hint ? &translation_hint->page[0] : virtual_target;
				memcpy(&hook->fake_page[0], data_source, PAGE_SIZE);

				hook->target_page->flags = hook->readwrite_entry.flags;
			}

			return hook;
		}

		hook = &this->allocate_ept_hook(physical_base_address);

		this->split_large_page(physical_address);

		const auto* data_source = translation_hint ? &translation_hint->page[0] : virtual_target;
		memcpy(&hook->fake_page[0], data_source, PAGE_SIZE);
		memcpy(&hook->diff_page[0], data_source, PAGE_SIZE);
		hook->physical_base_address = physical_base_address;

		hook->target_page = this->get_pml1_entry(physical_address);
		if (!hook->target_page)
		{
			throw std::runtime_error("Failed to get PML1 entry for target address");
		}

		hook->original_entry = *hook->target_page;
		hook->readwrite_entry = hook->original_entry;

		hook->readwrite_entry.read_access = 1;
		hook->readwrite_entry.write_access = 1;
		hook->readwrite_entry.execute_access = 0;

		hook->execute_entry.flags = 0;
		hook->execute_entry.read_access = 0;
		hook->execute_entry.write_access = 0;
		hook->execute_entry.execute_access = 1;
		hook->execute_entry.page_frame_number = memory::get_physical_address(&hook->fake_page) / PAGE_SIZE;

		hook->target_page->flags = hook->readwrite_entry.flags;

		return hook;
	}

	void ept::split_large_page(const uint64_t physical_address)
	{
		auto* target_entry = this->get_pml2_entry(physical_address);
		if (!target_entry)
		{
			throw std::runtime_error("Invalid physical address");
		}

		if (!target_entry->large_page)
		{
			return;
		}

		auto& split = this->allocate_ept_split();

		epte pml1_template{};
		pml1_template.flags = 0;
		pml1_template.read_access = 1;
		pml1_template.write_access = 1;
		pml1_template.execute_access = 1;
		pml1_template.memory_type = target_entry->memory_type;
		pml1_template.ignore_pat = target_entry->ignore_pat;
		pml1_template.suppress_ve = target_entry->suppress_ve;

		__stosq(reinterpret_cast<uint64_t*>(&split.pml1[0]), pml1_template.flags, EPT_PTE_ENTRY_COUNT);

		for (auto i = 0; i < EPT_PTE_ENTRY_COUNT; ++i)
		{
			split.pml1[i].page_frame_number = ((target_entry->page_frame_number * 2_mb) / PAGE_SIZE) + i;
		}

		pml2_ptr new_pointer{};
		new_pointer.flags = 0;
		new_pointer.read_access = 1;
		new_pointer.write_access = 1;
		new_pointer.execute_access = 1;

		new_pointer.page_frame_number = memory::get_physical_address(&split.pml1[0]) / PAGE_SIZE;

		target_entry->flags = new_pointer.flags;
	}

	utils::list<ept_translation_hint> ept::generate_translation_hints(const void* destination, const size_t length)
	{
		utils::list<ept_translation_hint> hints{};

		auto current_destination = reinterpret_cast<uint64_t>(destination);
		auto current_length = length;

		while (current_length != 0)
		{
			const auto aligned_destination = PAGE_ALIGN(current_destination);
			const auto page_offset = ADDRMASK_EPT_PML1_OFFSET(current_destination);
			const auto page_remaining = PAGE_SIZE - page_offset;
			const auto data_to_write = min(page_remaining, current_length);

			const auto physical_base_address = memory::get_physical_address(aligned_destination);
			if (!physical_base_address)
			{
				throw std::runtime_error("Failed to resolve physical address");
			}

			auto& current_hint = hints.emplace_back();

			current_hint.virtual_base_address = aligned_destination;
			current_hint.physical_base_address = physical_base_address;

			memcpy(&current_hint.page[0], aligned_destination, PAGE_SIZE);

			current_length -= data_to_write;
			current_destination += data_to_write;
		}

		return hints;
	}

	uint64_t* ept::get_access_records(size_t* count)
	{
		size_t i = 0;
		for (const auto& record : this->access_records)
		{
			if (record == 0) break;
			++i;
		}

		*count = i;
		return this->access_records;
	}

	bool ept::cleanup_process(const process_id process)
	{
		bool changed = false;

		for (auto i = this->ept_hooks.begin(); i != this->ept_hooks.end();)
		{
			if (i->source_pid == process || i->target_pid == process)
			{
				i = this->ept_hooks.erase(i);
				changed = true;
			}
			else
			{
				++i;
			}
		}

		for (auto i = this->ept_code_watch_points.begin(); i != this->ept_code_watch_points.end();)
		{
			if (i->source_pid == process || i->target_pid == process)
			{
				i = this->ept_code_watch_points.erase(i);
				changed = true;
			}
			else
			{
				++i;
			}
		}

		return changed;
	}
}
