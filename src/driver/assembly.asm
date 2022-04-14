include ksamd64.inc

.code

; -----------------------------------------------------

LEAF_ENTRY _str, _TEXT$00
    str word ptr [rcx]
    ret
LEAF_END _str, _TEXT$00

; -----------------------------------------------------

LEAF_ENTRY _sldt, _TEXT$00
    sldt word ptr [rcx]
    ret
LEAF_END _sldt, _TEXT$00

; -----------------------------------------------------

LEAF_ENTRY __lgdt, _TEXT$00
    lgdt    fword ptr [rcx]
    ret
LEAF_END __lgdt, _TEXT$00

; -----------------------------------------------------

LEAF_ENTRY __invept, _TEXT$00
    invept rcx, OWORD PTR [rdx]
    ret
LEAF_END __invept, _TEXT$00

; -----------------------------------------------------
    
LEAF_ENTRY restore_context, _TEXT$00
    movaps  xmm0, CxXmm0[rcx]
    movaps  xmm1, CxXmm1[rcx]
    movaps  xmm2, CxXmm2[rcx]
    movaps  xmm3, CxXmm3[rcx]
    movaps  xmm4, CxXmm4[rcx]
    movaps  xmm5, CxXmm5[rcx]
    movaps  xmm6, CxXmm6[rcx]
    movaps  xmm7, CxXmm7[rcx]
    movaps  xmm8, CxXmm8[rcx]
    movaps  xmm9, CxXmm9[rcx]
    movaps  xmm10, CxXmm10[rcx]
    movaps  xmm11, CxXmm11[rcx]
    movaps  xmm12, CxXmm12[rcx]
    movaps  xmm13, CxXmm13[rcx]
    movaps  xmm14, CxXmm14[rcx]
    movaps  xmm15, CxXmm15[rcx]
    ldmxcsr CxMxCsr[rcx]

    mov     rax, CxRax[rcx]
    mov     rdx, CxRdx[rcx]
    mov     r8, CxR8[rcx]
    mov     r9, CxR9[rcx]
    mov     r10, CxR10[rcx]
    mov     r11, CxR11[rcx]

    mov     rbx, CxRbx[rcx]
    mov     rsi, CxRsi[rcx]
    mov     rdi, CxRdi[rcx]
    mov     rbp, CxRbp[rcx]
    mov     r12, CxR12[rcx]
    mov     r13, CxR13[rcx]
    mov     r14, CxR14[rcx]
    mov     r15, CxR15[rcx]

    cli
    push    CxEFlags[rcx]
    popfq
    mov     rsp, CxRsp[rcx]
    push    CxRip[rcx]
    mov     rcx, CxRcx[rcx]
    ret
LEAF_END restore_context, _TEXT$00

; -----------------------------------------------------

extern vm_exit_handler:proc
extern vm_launch_handler:proc
extern RtlCaptureContext:proc

; -----------------------------------------------------

vm_launch PROC
    mov rcx, rsp
    sub rsp, 30h
    jmp vm_launch_handler
vm_launch ENDP

; -----------------------------------------------------

vm_exit PROC
    ; Load CONTEXT pointer
    push    rcx
    lea     rcx, [rsp+8h]

    sub rsp, 30h ; Home-space
    call RtlCaptureContext
    add rsp, 30h

    mov rcx, [rsp+CxRsp+8h]
    add rcx, 8h  ; Fixup push rcx
    add rcx, 30h ; Fixup home-space
    mov [rsp+CxRsp+8h], rcx

    pop rcx
    mov [rsp+CxRcx], rcx

    mov rcx, rsp
    sub rsp, 30h
    jmp vm_exit_handler
vm_exit ENDP

end
