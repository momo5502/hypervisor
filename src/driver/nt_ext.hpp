#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

// ----------------------------------------

NTKERNELAPI
_IRQL_requires_max_(APC_LEVEL)
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
KeGenericCallDpc(
	_In_ PKDEFERRED_ROUTINE Routine,
	_In_opt_ PVOID Context
);

// ----------------------------------------

NTKERNELAPI
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
KeSignalCallDpcDone(
	_In_ PVOID SystemArgument1
);

// ----------------------------------------

NTKERNELAPI
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
LOGICAL
KeSignalCallDpcSynchronize(
	_In_ PVOID SystemArgument2
);

// ----------------------------------------

#if (NTDDI_VERSION < NTDDI_WIN8)
_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTKERNELAPI
_When_(return != NULL, _Post_writable_byte_size_ (NumberOfBytes)) PVOID
MmAllocateContiguousNodeMemory(
	_In_ SIZE_T NumberOfBytes,
	_In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
	_In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
	_In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple,
	_In_ ULONG Protect,
	_In_ NODE_REQUIREMENT PreferredNode
);
#endif

// ----------------------------------------

NTSYSAPI
VOID
NTAPI
RtlCaptureContext(
	_Out_ PCONTEXT ContextRecord
);

// ----------------------------------------

typedef struct _KAPC_STATE
{
	LIST_ENTRY ApcListHead[MaximumMode];
	struct _KPROCESS* Process;
	BOOLEAN KernelApcInProgress;
	BOOLEAN KernelApcPending;
	BOOLEAN UserApcPending;
} KAPC_STATE, *PKAPC_STATE, *PRKAPC_STATE;

// ----------------------------------------

NTKERNELAPI
VOID
KeStackAttachProcess(
	__inout PEPROCESS PROCESS,
	__out PRKAPC_STATE ApcState
);

// ----------------------------------------

NTKERNELAPI
VOID
KeUnstackDetachProcess(
	__in PRKAPC_STATE ApcState
);

// ----------------------------------------

NTKERNELAPI
NTSTATUS
PsLookupProcessByProcessId(
	IN HANDLE ProcessId,
	OUT PEPROCESS* Process
);

// ----------------------------------------

NTKERNELAPI
PVOID
PsGetProcessSectionBaseAddress(
	__in PEPROCESS Process
);

// ----------------------------------------

NTKERNELAPI
PPEB
NTAPI
PsGetProcessPeb(
	IN PEPROCESS Process
);

// ----------------------------------------

NTKERNELAPI
PCSTR
PsGetProcessImageFileName(
	__in PEPROCESS Process
);

// ----------------------------------------

__kernel_entry NTSYSCALLAPI
NTSTATUS
NTAPI
NtCreateFile(
	_Out_ PHANDLE FileHandle,
	_In_ ACCESS_MASK DesiredAccess,
	_In_ POBJECT_ATTRIBUTES ObjectAttributes,
	_Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_In_opt_ PLARGE_INTEGER AllocationSize,
	_In_ ULONG FileAttributes,
	_In_ ULONG ShareAccess,
	_In_ ULONG CreateDisposition,
	_In_ ULONG CreateOptions,
	_In_reads_bytes_opt_(EaLength) PVOID EaBuffer,
	_In_ ULONG EaLength
);

#ifdef __cplusplus
}
#endif
