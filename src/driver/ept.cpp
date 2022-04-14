#include "std_include.hpp"
#include "ept.hpp"

#include "assembly.hpp"
#include "memory.hpp"
#include "vmx.hpp"

#define MTRR_PAGE_SIZE 4096
#define MTRR_PAGE_MASK (~(MTRR_PAGE_SIZE-1))

#define ADDRMASK_EPT_PML1_OFFSET(_VAR_) (_VAR_ & 0xFFFULL)

#define ADDRMASK_EPT_PML1_INDEX(_VAR_) ((_VAR_ & 0x1FF000ULL) >> 12)
#define ADDRMASK_EPT_PML2_INDEX(_VAR_) ((_VAR_ & 0x3FE00000ULL) >> 21)
#define ADDRMASK_EPT_PML3_INDEX(_VAR_) ((_VAR_ & 0x7FC0000000ULL) >> 30)
#define ADDRMASK_EPT_PML4_INDEX(_VAR_) ((_VAR_ & 0xFF8000000000ULL) >> 39)

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
			//
			// Read the capabilities mask
			//
			ia32_mtrr_capabilities_register mtrr_capabilities{};
			mtrr_capabilities.flags = __readmsr(IA32_MTRR_CAPABILITIES);

			//
			// Iterate over each variable MTRR
			//
			for (auto i = 0u; i < mtrr_capabilities.variable_range_count; i++)
			{
				//
				// Capture the value
				//
				ia32_mtrr_physbase_register mtrr_base{};
				ia32_mtrr_physmask_register mtrr_mask{};

				mtrr_base.flags = __readmsr(IA32_MTRR_PHYSBASE0 + i * 2);
				mtrr_mask.flags = __readmsr(IA32_MTRR_PHYSMASK0 + i * 2);

				//
				// Check if the MTRR is enabled
				//
				mtrr_data[i].type = static_cast<uint32_t>(mtrr_base.type);
				mtrr_data[i].enabled = static_cast<uint32_t>(mtrr_mask.valid);
				if (mtrr_data[i].enabled != FALSE)
				{
					//
					// Set the base
					//
					mtrr_data[i].physical_address_min = mtrr_base.page_frame_number *
						MTRR_PAGE_SIZE;

					//
					// Compute the length
					//
					unsigned long bit;
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
			//
			// Loop each MTRR range
			//
			for (const auto& mtrr_entry : mtrr_data)
			{
				//
				// Check if it's active
				//
				if (!mtrr_entry.enabled)
				{
					continue;
				}
				//
				// Check if this large page falls within the boundary. If a single
				// physical page (4KB) touches it, we need to override the entire 2MB.
				//
				if (((large_page_address + (2_mb - 1)) >= mtrr_entry.physical_address_min) &&
					(large_page_address <= mtrr_entry.physical_address_max))
				{
					candidate_memory_type = mtrr_entry.type;
				}
			}

			return candidate_memory_type;
		}
	}

	ept::ept()
	{
	}

	ept::~ept()
	{
		auto* split = this->ept_splits;
		while (split)
		{
			auto* current_split = split;
			split = split->next_split;
			memory::free_aligned_object(current_split);
		}

		auto* hook = this->ept_hooks;
		while (hook)
		{
			auto* current_hook = hook;
			hook = hook->next_hook;
			memory::free_aligned_object(current_hook);
		}
	}

	void ept::install_page_hook(void* destination, const void* source, const size_t length)
	{
		auto* hook = this->get_or_create_ept_hook(destination);

		const auto page_offset = ADDRMASK_EPT_PML1_OFFSET(reinterpret_cast<uint64_t>(destination));
		memcpy(hook->fake_page + page_offset, source, length);
	}

	void ept::install_hook(void* destination, const void* source, const size_t length)
	{
		auto current_destination = reinterpret_cast<uint64_t>(destination);
		auto current_source = reinterpret_cast<uint64_t>(source);
		auto current_length = length;

		while (current_length != 0)
		{
			const auto page_offset = ADDRMASK_EPT_PML1_OFFSET(current_destination);
			const auto page_remaining = PAGE_SIZE - page_offset;
			const auto data_to_write = min(page_remaining, current_length);

			this->install_page_hook(reinterpret_cast<void*>(current_destination),
			                        reinterpret_cast<const void*>(current_source), data_to_write);

			current_length -= data_to_write;
			current_destination += data_to_write;
			current_source += data_to_write;
		}
	}

	void ept::disable_all_hooks() const
	{
		auto* hook = this->ept_hooks;
		while (hook)
		{
			hook->target_page->flags = hook->original_entry.flags;
			hook = hook->next_hook;
		}
	}

	void ept::handle_violation(guest_context& guest_context) const
	{
		vmx_exit_qualification_ept_violation violation_qualification{};
		violation_qualification.flags = guest_context.exit_qualification;

		if (!violation_qualification.caused_by_translation)
		{
			guest_context.exit_vm = true;
		}

		auto* hook = this->ept_hooks;
		while (hook)
		{
			if (hook->physical_base_address == reinterpret_cast<uint64_t>(PAGE_ALIGN(
				guest_context.guest_physical_address)))
			{
				break;
			}
			hook = hook->next_hook;
		}

		if (!hook)
		{
			return;
		}

		if (!violation_qualification.ept_executable && violation_qualification.execute_access)
		{
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

		epdpte temp_epdpte;
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
		auto* pml1 = static_cast<epte*>(memory::get_virtual_address(pml2->page_frame_number * PAGE_SIZE));
		if (!pml1)
		{
			pml1 = this->find_pml1_table(pml2->page_frame_number * PAGE_SIZE);
		}

		if (!pml1)
		{
			return nullptr;
		}

		return &pml1[ADDRMASK_EPT_PML1_INDEX(physical_address)];
	}

	pml1* ept::find_pml1_table(const uint64_t physical_address) const
	{
		auto* split = this->ept_splits;
		while (split)
		{
			if (memory::get_physical_address(&split->pml1[0]) == physical_address)
			{
				return split->pml1;
			}

			split = split->next_split;
		}

		return nullptr;
	}

	ept_split* ept::allocate_ept_split()
	{
		auto* split = memory::allocate_aligned_object<ept_split>();
		if (!split)
		{
			throw std::runtime_error("Failed to allocate ept split object");
		}

		split->next_split = this->ept_splits;
		this->ept_splits = split;

		return split;
	}

	ept_hook* ept::allocate_ept_hook()
	{
		auto* hook = memory::allocate_aligned_object<ept_hook>();
		if (!hook)
		{
			throw std::runtime_error("Failed to allocate ept hook object");
		}

		hook->next_hook = this->ept_hooks;
		this->ept_hooks = hook;

		return hook;
	}

	ept_hook* ept::find_ept_hook(const uint64_t physical_address) const
	{
		auto* hook = this->ept_hooks;
		while (hook)
		{
			if (hook->physical_base_address == physical_address)
			{
				return hook;
			}

			hook = hook->next_hook;
		}

		return nullptr;
	}

	ept_hook* ept::get_or_create_ept_hook(void* destination)
	{
		const auto virtual_target = PAGE_ALIGN(destination);
		const auto physical_address = memory::get_physical_address(virtual_target);

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
				hook->target_page->flags = hook->readwrite_entry.flags;
			}

			return hook;
		}

		hook = this->allocate_ept_hook();

		if (!hook)
		{
			throw std::runtime_error("Failed to allocate hook");
		}

		this->split_large_page(physical_address);

		memcpy(&hook->fake_page[0], virtual_target, PAGE_SIZE);
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

		auto* split = this->allocate_ept_split();

		epte pml1_template{};
		pml1_template.flags = 0;
		pml1_template.read_access = 1;
		pml1_template.write_access = 1;
		pml1_template.execute_access = 1;
		pml1_template.memory_type = target_entry->memory_type;
		pml1_template.ignore_pat = target_entry->ignore_pat;
		pml1_template.suppress_ve = target_entry->suppress_ve;

		__stosq(reinterpret_cast<uint64_t*>(&split->pml1[0]), pml1_template.flags, EPT_PTE_ENTRY_COUNT);

		for (auto i = 0; i < EPT_PTE_ENTRY_COUNT; ++i)
		{
			split->pml1[i].page_frame_number = ((target_entry->page_frame_number * 2_mb) / PAGE_SIZE) + i;
		}

		pml2_ptr new_pointer{};
		new_pointer.flags = 0;
		new_pointer.read_access = 1;
		new_pointer.write_access = 1;
		new_pointer.execute_access = 1;

		new_pointer.page_frame_number = memory::get_physical_address(&split->pml1[0]) / PAGE_SIZE;

		target_entry->flags = new_pointer.flags;
	}
}
