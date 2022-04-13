#include "std_include.hpp"
#include "ept.hpp"

#include "memory.hpp"

#define MTRR_PAGE_SIZE 4096
#define MTRR_PAGE_MASK (~(MTRR_PAGE_SIZE-1))

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
	}

	void ept::initialize()
	{
		mtrr_list mtrr_data{};
		initialize_mtrr(mtrr_data);

		//
		// Fill out the EPML4E which covers the first 512GB of RAM
		//
		this->epml4[0].read_access = 1;
		this->epml4[0].write_access = 1;
		this->epml4[0].execute_access = 1;
		this->epml4[0].page_frame_number = memory::get_physical_address(&this->epdpt) /
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
		__stosq(reinterpret_cast<uint64_t*>(this->epdpt), temp_epdpte.flags, EPT_PDPTE_ENTRY_COUNT);
		for (auto i = 0; i < EPT_PDPTE_ENTRY_COUNT; i++)
		{
			//
			// Set the page frame number of the PDE table
			//
			this->epdpt[i].page_frame_number = memory::get_physical_address(&this->epde[i][0]) / PAGE_SIZE;
		}

		//
		// Fill out a RWX Large PDE
		//
		epde_2mb temp_epde{};
		temp_epde.flags = 0;
		temp_epde.read_access = 1;
		temp_epde.write_access = 1;
		temp_epde.execute_access = 1;
		temp_epde.large_page = 1;

		//
		// Loop every 1GB of RAM (described by the PDPTE)
		//
		__stosq(reinterpret_cast<uint64_t*>(this->epde), temp_epde.flags,
		        EPT_PDPTE_ENTRY_COUNT * EPT_PDE_ENTRY_COUNT);
		for (auto i = 0; i < EPT_PDPTE_ENTRY_COUNT; i++)
		{
			//
			// Construct EPT identity map for every 2MB of RAM
			//
			for (auto j = 0; j < EPT_PDE_ENTRY_COUNT; j++)
			{
				this->epde[i][j].page_frame_number = (i * 512) + j;
				this->epde[i][j].memory_type = mtrr_adjust_effective_memory_type(
					mtrr_data, this->epde[i][j].page_frame_number * 2_mb, MEMORY_TYPE_WRITE_BACK);
			}
		}
	}

	ept_pml4* ept::get_pml4()
	{
		return this->epml4;
	}

	const ept_pml4* ept::get_pml4() const
	{
		return this->epml4;
	}
}
