#include "std_include.hpp"
#include "ept.hpp"

#include "logging.hpp"
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

		NTSTATUS (*NtCreateFileOrig)(
			PHANDLE FileHandle,
			ACCESS_MASK DesiredAccess,
			POBJECT_ATTRIBUTES ObjectAttributes,
			PIO_STATUS_BLOCK IoStatusBlock,
			PLARGE_INTEGER AllocationSize,
			ULONG FileAttributes,
			ULONG ShareAccess,
			ULONG CreateDisposition,
			ULONG CreateOptions,
			PVOID EaBuffer,
			ULONG EaLength
		);

		NTSTATUS NtCreateFileHook(
			PHANDLE FileHandle,
			ACCESS_MASK DesiredAccess,
			POBJECT_ATTRIBUTES ObjectAttributes,
			PIO_STATUS_BLOCK IoStatusBlock,
			PLARGE_INTEGER AllocationSize,
			ULONG FileAttributes,
			ULONG ShareAccess,
			ULONG CreateDisposition,
			ULONG CreateOptions,
			PVOID EaBuffer,
			ULONG EaLength
		)
		{
			static WCHAR BlockedFileName[] = L"test.txt";
			static SIZE_T BlockedFileNameLength = (sizeof(BlockedFileName) / sizeof(BlockedFileName[0])) - 1;

			PWCH NameBuffer;
			USHORT NameLength;

			__try
			{
				ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
				ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);

				NameBuffer = ObjectAttributes->ObjectName->Buffer;
				NameLength = ObjectAttributes->ObjectName->Length;

				ProbeForRead(NameBuffer, NameLength, 1);

				/* Convert to length in WCHARs */
				NameLength /= sizeof(WCHAR);

				/* Does the file path (ignoring case and null terminator) end with our blocked file name? */
				if (NameLength >= BlockedFileNameLength &&
					_wcsnicmp(&NameBuffer[NameLength - BlockedFileNameLength], BlockedFileName,
					          BlockedFileNameLength) == 0)
				{
					return STATUS_ACCESS_DENIED;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				NOTHING;
			}

			return NtCreateFileOrig(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
			                        FileAttributes,
			                        ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
		}

		VOID HvEptHookWriteAbsoluteJump(uint8_t* TargetBuffer, SIZE_T TargetAddress)
		{
			/**
			 *   Use 'push ret' instead of 'jmp qword[rip+0]',
			 *   Because 'jmp qword[rip+0]' will read hooked page 8bytes.
			 *
			 *   14 bytes hook:
			 *   0x68 0x12345678 ......................push 'low 32bit of TargetAddress'
			 *   0xC7 0x44 0x24 0x04 0x12345678........mov dword[rsp + 4], 'high 32bit of TargetAddress'
			 *   0xC3..................................ret
			 */

			UINT32 Low32;
			UINT32 High32;

			Low32 = (UINT32)TargetAddress;
			High32 = (UINT32)(TargetAddress >> 32);

			/* push 'low 32bit of TargetAddress' */
			TargetBuffer[0] = 0x68;
			*((UINT32*)&TargetBuffer[1]) = Low32;

			/* mov dword[rsp + 4], 'high 32bit of TargetAddress' */
			*((UINT32*)&TargetBuffer[5]) = 0x042444C7;
			*((UINT32*)&TargetBuffer[9]) = High32;

			/* ret */
			TargetBuffer[13] = 0xC3;
		}

		bool HvEptHookInstructionMemory(ept_hook* Hook, PVOID TargetFunction, PVOID HookFunction,
		                                PVOID* OrigFunction)
		{
			SIZE_T OffsetIntoPage;

			OffsetIntoPage = ADDRMASK_EPT_PML1_OFFSET((SIZE_T)TargetFunction);

			if ((OffsetIntoPage + 14) > PAGE_SIZE - 1)
			{
				debug_log(
					"Function extends past a page boundary. We just don't have the technology to solve this.....\n");
				return FALSE;
			}

			const uint8_t fixup[] = {
				0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x33, 0xC0, 0x48, 0x89, 0x44, 0x24, 0x78
			};

			//HvUtilLogDebug("Number of bytes of instruction mem: %d\n", SizeOfHookedInstructions);

			/* Build a trampoline */

			/* Allocate some executable memory for the trampoline */
			Hook->trampoline = (uint8_t*)memory::allocate_non_paged_memory(sizeof(fixup) + 14);

			if (!Hook->trampoline)
			{
				debug_log("Could not allocate trampoline function buffer.\n");
				return FALSE;
			}

			/* Copy the trampoline instructions in. */
			RtlCopyMemory(Hook->trampoline, TargetFunction, sizeof(fixup));

			/* Add the absolute jump back to the original function. */
			HvEptHookWriteAbsoluteJump((&Hook->trampoline[sizeof(fixup)]),
			                           (SIZE_T)TargetFunction + sizeof(fixup));

			debug_log("Trampoline: 0x%llx\n", Hook->trampoline);
			debug_log("HookFunction: 0x%llx\n", HookFunction);

			/* Let the hook function call the original function */
			*OrigFunction = Hook->trampoline;

			/* Write the absolute jump to our shadow page memory to jump to our hook. */
			HvEptHookWriteAbsoluteJump(&Hook->fake_page[OffsetIntoPage], (SIZE_T)HookFunction);

			return TRUE;
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
			memory::free_non_paged_memory(current_hook->trampoline);
			memory::free_aligned_object(current_hook);
		}
	}

	void ept::install_hook(PVOID TargetFunction, PVOID HookFunction, PVOID* OrigFunction)
	{
		const auto VirtualTarget = PAGE_ALIGN(TargetFunction);
		const auto PhysicalAddress = memory::get_physical_address(VirtualTarget);

		if (!PhysicalAddress)
		{
			debug_log("HvEptAddPageHook: Target address could not be mapped to physical memory!\n");
			return;
		}

		/* Create a hook object*/
		auto* NewHook = this->allocate_ept_hook();

		if (!NewHook)
		{
			debug_log("HvEptAddPageHook: Could not allocate memory for new hook.\n");
			return;
		}

		/* 
		 * Ensure the page is split into 512 4096 byte page entries. We can only hook a 4096 byte page, not a 2MB page.
		 * This is due to performance hit we would get from hooking a 2MB page.
		 */
		this->split_large_page(PhysicalAddress);
		RtlCopyMemory(&NewHook->fake_page[0], VirtualTarget, PAGE_SIZE);

		/* Base address of the 4096 page. */
		NewHook->physical_base_address = (SIZE_T)PAGE_ALIGN(PhysicalAddress);

		/* Pointer to the page entry in the page table. */
		NewHook->target_page = this->get_pml1_entry(PhysicalAddress);

		/* Ensure the target is valid. */
		if (!NewHook->target_page)
		{
			debug_log("HvEptAddPageHook: Failed to get PML1 entry for target address.\n");
			return;
		}

		/* Save the original permissions of the page */
		NewHook->original_entry = *NewHook->target_page;
		auto OriginalEntry = *NewHook->target_page;

		/* Setup the new fake page table entry */
		pml1 FakeEntry{};
		FakeEntry.flags = 0;

		/* We want this page to raise an EPT violation on RW so we can handle by swapping in the original page. */
		FakeEntry.read_access = 0;
		FakeEntry.write_access = 0;
		FakeEntry.execute_access = 1;

		/* Point to our fake page we just made */
		FakeEntry.page_frame_number = memory::get_physical_address(&NewHook->fake_page) / PAGE_SIZE;

		/* Save a copy of the fake entry. */
		NewHook->shadow_entry.flags = FakeEntry.flags;

		/* 
		 * Lastly, mark the entry in the table as no execute. This will cause the next time that an instruction is
		 * fetched from this page to cause an EPT violation exit. This will allow us to swap in the fake page with our
		 * hook.
		 */
		OriginalEntry.read_access = 1;
		OriginalEntry.write_access = 1;
		OriginalEntry.execute_access = 0;

		/* The hooked entry will be swapped in first. */
		NewHook->hooked_entry.flags = OriginalEntry.flags;

		if (!HvEptHookInstructionMemory(NewHook, TargetFunction, HookFunction, OrigFunction))
		{
			debug_log("HvEptAddPageHook: Could not build hook.\n");
			return;
		}

		/* Apply the hook to EPT */
		NewHook->target_page->flags = OriginalEntry.flags;

		/*
		 * Invalidate the entry in the TLB caches so it will not conflict with the actual paging structure.
		 */
		/*if (ProcessorContext->HasLaunched)
		{
			Descriptor.EptPointer = ProcessorContext->EptPointer.Flags;
			Descriptor.Reserved = 0;
			__invept(1, &Descriptor);
		}*/
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
			hook->target_page->flags = hook->shadow_entry.flags;
			guest_context.increment_rip = false;
		}

		if (violation_qualification.ept_executable && (violation_qualification.read_access || violation_qualification.
			write_access))
		{
			hook->target_page->flags = hook->hooked_entry.flags;
			guest_context.increment_rip = false;
		}
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

		this->install_hook((PVOID)NtCreateFile, (PVOID)NtCreateFileHook, (PVOID*)&NtCreateFileOrig);
	}

	ept_pml4* ept::get_pml4()
	{
		return this->epml4;
	}

	const ept_pml4* ept::get_pml4() const
	{
		return this->epml4;
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
