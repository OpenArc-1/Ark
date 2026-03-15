/*
 * arch/x86_64/syscall64.S — Ark x86_64 SYSCALL fast-path
 *
 *  x86_64 syscall ABI:
 *   rax=nr  rdi=a1  rsi=a2  rdx=a3  r10=a4  r8=a5  r9=a6
 *   rcx = saved RIP (clobbered by SYSCALL)
 *   r11 = saved RFLAGS (clobbered by SYSCALL)
 *
 * C dispatch signature:
 *   u64 syscall64_dispatch(u64 nr, u64 a1, u64 a2, u64 a3,
 *                          u64 a4, u64 a5, u64 a6);
 */

.extern isr64_common

.section .text
.code64

/* ──────────────────────────────────────────────────────────────────────────
 * syscall64_entry — LSTAR target
 * ──────────────────────────────────────────────────────────────────────────
 * On entry:  rax=nr, rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5, r9=a6
 *            rcx=saved_rip, r11=saved_rflags, rsp=caller stack
 */
.globl syscall64_entry
.type  syscall64_entry, @function
syscall64_entry:
    /* Switch to dedicated kernel syscall stack */
    movq  %rsp, syscall64_user_rsp(%rip)
    leaq  syscall64_kstack_top(%rip), %rsp

    /* Save SYSRET state and callee-saved regs */
    pushq %rcx                   /* saved user RIP   → needed by SYSRET */
    pushq %r11                   /* saved user RFLAGS → needed by SYSRET */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * Build C call: syscall64_dispatch(nr, a1, a2, a3, a4, a5, a6)
     *
     * Incoming:  rax=nr  rdi=a1  rsi=a2  rdx=a3  r10=a4  r8=a5  r9=a6
     * C ABI:     rdi=nr  rsi=a1  rdx=a2  rcx=a3  r8=a4   r9=a5  (a6 on stack)
     *
     * Shift args one position to the right:
     */
    pushq %r9                    /* a6 on stack (7th C arg)             */
    movq  %r8,  %r9              /* a5 → r9                             */
    movq  %r10, %r8              /* a4 → r8                             */
    movq  %rdx, %rcx             /* a3 → rcx                            */
    movq  %rsi, %rdx             /* a2 → rdx                            */
    movq  %rdi, %rsi             /* a1 → rsi                            */
    movq  %rax, %rdi             /* nr → rdi                            */

    cld
    call  syscall64_dispatch
    addq  $8, %rsp               /* pop a6 from stack                   */
    /* result in rax */

    /* Restore callee-saved regs */
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rbp
    popq  %rbx
    popq  %r11                   /* user RFLAGS                         */
    popq  %rcx                   /* user RIP                            */

    /* Restore caller stack */
    movq  syscall64_user_rsp(%rip), %rsp

    /* Return to userspace */
    sysretq

/* ──────────────────────────────────────────────────────────────────────────
 * int 0x80 compat bridge — routes through the IDT common handler
 * ──────────────────────────────────────────────────────────────────────────
 */
.globl syscall64_int80_compat
syscall64_int80_compat:
    pushq $0
    pushq $0x80
    jmp   isr64_common

/* ──────────────────────────────────────────────────────────────────────────
 * syscall64_install — program SYSCALL MSRs
 * Called once from arch_x86_64_entry() after idt64_init().
 * ──────────────────────────────────────────────────────────────────────────
 */
.globl syscall64_install
.type  syscall64_install, @function
syscall64_install:
    pushq %rbx

    /* ── 1. EFER.SCE = 1  (IA32_EFER = 0xC0000080) ── */
    movl  $0xC0000080, %ecx
    rdmsr
    orl   $0x01, %eax
    wrmsr

    /* ── 2. STAR (0xC0000081) ──
     *   STAR[47:32] = 0x0018  kernel CS on SYSCALL
     *   STAR[63:48] = 0x0023  user   CS base for SYSRET
     *     → SYSRET64 sets CS = STAR[63:48]+16 = 0x33 (user 64-bit code)
     *                     SS = STAR[63:48]+8  = 0x2B (user data)
     */
    movl  $0xC0000081, %ecx
    xorl  %eax, %eax
    movl  $0x00230018, %edx
    wrmsr

    /* ── 3. LSTAR (0xC0000082) = physical address of syscall64_entry ── */
    movl  $0xC0000082, %ecx
    leaq  syscall64_entry(%rip), %rbx
    movl  %ebx, %eax            /* low  32 bits                        */
    shrq  $32, %rbx
    movl  %ebx, %edx            /* high 32 bits                        */
    wrmsr

    /* ── 4. FMASK (0xC0000084) — rflags bits to clear on SYSCALL ──
     *   bit 9  (IF) — disable interrupts during syscall
     *   bit 10 (DF) — clear direction flag
     */
    movl  $0xC0000084, %ecx
    movl  $0x00000600, %eax
    xorl  %edx, %edx
    wrmsr

    /* ── 5. IA32_GS_BASE (0xC0000101) — point GS at our scratch area ── */
    movl  $0xC0000101, %ecx
    leaq  syscall64_scratch(%rip), %rbx
    /* pre-fill kernel RSP slot at scratch+8 */
    leaq  syscall64_kstack_top(%rip), %rax
    movq  %rax, 8(%rbx)
    movl  %ebx, %eax
    shrq  $32, %rbx
    movl  %ebx, %edx
    wrmsr

    popq  %rbx
    ret

/* ──────────────────────────────────────────────────────────────────────────
 * Data / BSS
 * ──────────────────────────────────────────────────────────────────────────
 */
.section .data
.align 8
syscall64_user_rsp:
    .quad 0                      /* saved caller RSP during syscall     */

.section .bss
.align 64
.globl syscall64_scratch
syscall64_scratch:
    .skip 64                     /* GS-relative scratch (future use)    */

.align 16
syscall64_kstack_bottom:
    .skip 16384                  /* 16 KiB dedicated kernel syscall stack */
.globl syscall64_kstack_top
syscall64_kstack_top:

.section .note.GNU-stack, "", @progbits
