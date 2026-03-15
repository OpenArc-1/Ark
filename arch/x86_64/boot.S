/*
 * arch/x86_64/boot.S — Ark kernel x86_64 entry (Multiboot2 + PVH)
 *
 * Multiboot2 entry state:
 *   32-bit protected mode, paging OFF
 *   EAX = 0x36d76289, EBX = multiboot2 info ptr
 *   IF=0, CS/DS/SS = flat 32-bit segments
 */

/* ── PVH ELF Note ──────────────────────────────────────────────────────── */
.section .note.Xen, "a", @note
.align 4
    .long 4
    .long 4
    .long 18
    .ascii "Xen\0"
    .long  pvh_start_xen

/* ── Multiboot2 Header ─────────────────────────────────────────────────── */
.section .multiboot, "a"
.align 8
.Lmb2_start:
    .long  0xe85250d6
    .long  0
    .long  .Lmb2_end - .Lmb2_start
    .long  -(0xe85250d6 + 0 + (.Lmb2_end - .Lmb2_start))
    .short 0
    .short 0
    .long  8
.Lmb2_end:

/* ── 32-bit bootstrap ──────────────────────────────────────────────────── */
.section .text.boot, "ax"
.code32

.globl pvh_start_xen
pvh_start_xen:
    xorl %eax, %eax
    xorl %ebx, %ebx

.globl _start
_start:
    cli
    cld

    /* Save multiboot regs immediately */
    movl %eax, saved_magic
    movl %ebx, saved_mb_info

    /* ── Disable ALL hardware breakpoints FIRST (DR7=0 before touching DR0-DR3) ── */
    xorl %eax, %eax
    movl %eax, %dr7     /* disable breakpoints before clearing DR0-DR3 */
    movl %eax, %dr0
    movl %eax, %dr1
    movl %eax, %dr2
    movl %eax, %dr3
    movl %eax, %dr6

    /* ── Load our GDT ── */
    lgdt gdt32_ptr
    ljmp $0x08, $.Lcs32
.Lcs32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    /* ── Zero page tables (6 tables × 4096 bytes = 6144 dwords) ── */
    movl $pml4, %edi
    xorl %eax,  %eax
    movl $6144, %ecx
    rep  stosl

    /* ── PML4[0] → PDPT ── */
    movl $pdpt, %eax
    orl  $3,    %eax
    movl %eax,  pml4

    /* ── PDPT[0] → PD (covers 0–1 GiB, kernel + low RAM) ── */
    movl $pd,   %eax
    orl  $3,    %eax
    movl %eax,  pdpt

    /* ── PDPT[1] → pd_mid1 (covers 1–2 GiB) ── */
    movl $pd_mid1, %eax
    orl  $3,       %eax
    movl %eax,     pdpt + 1*8

    /* ── PDPT[2] → pd_mid2 (covers 2–3 GiB) ── */
    movl $pd_mid2, %eax
    orl  $3,       %eax
    movl %eax,     pdpt + 2*8

    /* ── PDPT[3] → pd_hi (covers 3–4 GiB: 0xC0000000–0xFFFFFFFF)
     * VirtualBox LFB at ~0xE0000000, QEMU fallback at 0xFD000000 */
    movl $pd_hi, %eax
    orl  $3,     %eax
    movl %eax,   pdpt + 3*8

    /* ── Fill PD: 512 × 2 MiB huge pages = 1 GiB identity map (0–1 GiB) ── */
    movl $pd,         %edi
    movl $0x00000083, %eax      /* P | RW | PS */
    xorl %edx,        %edx
    movl $512,        %ecx
.Lfill:
    movl %eax,  (%edi)
    movl %edx, 4(%edi)
    addl $0x200000, %eax
    adcl $0,        %edx
    addl $8,        %edi
    decl %ecx
    jnz  .Lfill

    /* ── Fill pd_mid1: 512 × 2 MiB starting at 0x40000000 (1–2 GiB) ── */
    movl $pd_mid1,    %edi
    movl $0x40000083, %eax
    xorl %edx,        %edx
    movl $512,        %ecx
.Lfill_mid1:
    movl %eax,  (%edi)
    movl %edx, 4(%edi)
    addl $0x200000, %eax
    adcl $0,        %edx
    addl $8,        %edi
    decl %ecx
    jnz  .Lfill_mid1

    /* ── Fill pd_mid2: 512 × 2 MiB starting at 0x80000000 (2–3 GiB) ── */
    movl $pd_mid2,    %edi
    movl $0x80000083, %eax
    xorl %edx,        %edx
    movl $512,        %ecx
.Lfill_mid2:
    movl %eax,  (%edi)
    movl %edx, 4(%edi)
    addl $0x200000, %eax
    adcl $0,        %edx
    addl $8,        %edi
    decl %ecx
    jnz  .Lfill_mid2

    /* ── Fill pd_hi: 512 × 2 MiB huge pages starting at 0xC0000000 ── */
    movl $pd_hi,      %edi
    movl $0xC0000083, %eax      /* P | RW | PS | base=0xC0000000 */
    movl $0,          %edx
    movl $512,        %ecx
.Lfill_hi:
    movl %eax,  (%edi)
    movl %edx, 4(%edi)
    addl $0x200000, %eax
    adcl $0,        %edx
    addl $8,        %edi
    decl %ecx
    jnz  .Lfill_hi

    /* ── Enable PAE ── */
    movl %cr4, %eax
    orl  $0x20, %eax
    movl %eax, %cr4

    /* ── Load CR3 ── */
    movl $pml4, %eax
    movl %eax,  %cr3

    /* ── EFER.LME = 1 ── */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x100, %eax
    wrmsr

    /* ── Enable paging → long mode ── */
    movl %cr0, %eax
    orl  $0x80000001, %eax
    movl %eax, %cr0

    /* ── Far jump into 64-bit code segment (CS = 0x18) ── */
    ljmp $0x18, $.Lentry64

/* ── 64-bit long mode entry ────────────────────────────────────────────── */
.code64
.Lentry64:
    movw $0x20, %ax
    movw %ax,   %ds
    movw %ax,   %es
    movw %ax,   %ss
    xorw %ax,   %ax
    movw %ax,   %fs
    movw %ax,   %gs

    /* Reload GDTR with 64-bit base (RIP-relative, correct in long mode) */
    lgdt gdt64_ptr(%rip)

    /* ── Set up kernel stack FIRST ── */
    leaq stack_top(%rip), %rsp
    xorq %rbp, %rbp

    /* ── Set RFLAGS to a clean known state (bit 1 = reserved, must be 1) ── */
    pushq $0x2          /* IF=0, TF=0, all flags clear, reserved bit 1 set */
    popfq

    /* ── Zero BSS with rep stosq (single burst instruction, ~8 bytes/cycle) ── */
    leaq _bss_start(%rip), %rdi
    leaq _bss_end(%rip),   %rcx
    subq %rdi, %rcx
    shrq $3,   %rcx              /* convert bytes → qwords */
    xorq %rax, %rax
    rep stosq

    /* Stack is now zeroed but RSP still valid (points to top, not used yet) */

    /* ── Reload magic/mbinfo (were in .data, not zeroed) ── */
    movl saved_magic(%rip),   %edi
    movl saved_mb_info(%rip), %esi

    cld
    call arch_x86_64_entry

.Lhang:
    cli
    hlt
    jmp .Lhang

/* ── GDT ───────────────────────────────────────────────────────────────── */
.section .data
.align 8
gdt64:
    .quad 0x0000000000000000    /* 0x00: null            */
    .quad 0x00CF9A000000FFFF    /* 0x08: 32-bit code     */
    .quad 0x00CF92000000FFFF    /* 0x10: 32-bit data     */
    .quad 0x00AF9A000000FFFF    /* 0x18: 64-bit code L=1 */
    .quad 0x00CF92000000FFFF    /* 0x20: 64-bit data     */
    .quad 0x00AFFA000000FFFF    /* 0x28: user 64-bit code */
    .quad 0x00CFF2000000FFFF    /* 0x30: user 64-bit data */
.Lgdt64_end:

/* 32-bit GDTR — 6 bytes: 2-byte limit + 4-byte base (pre-paging) */
gdt32_ptr:
    .word .Lgdt64_end - gdt64 - 1
    .long gdt64

/* 64-bit GDTR — 10 bytes: 2-byte limit + 8-byte base */
gdt64_ptr:
    .word .Lgdt64_end - gdt64 - 1
    .quad gdt64

saved_magic:   .long 0
saved_mb_info: .long 0

.align 8
.globl efi_system_table_ptr
efi_system_table_ptr:
    .quad 0

/* ── Page tables in .data (NOT .bss — must survive BSS zero pass) ──────── */
.align 4096
.globl pml4
pml4:   .fill 4096, 1, 0
pdpt:   .fill 4096, 1, 0
pd:     .fill 4096, 1, 0       /* 0–1 GiB   */
pd_mid1:.fill 4096, 1, 0       /* 1–2 GiB   */
pd_mid2:.fill 4096, 1, 0       /* 2–3 GiB   */
pd_hi:  .fill 4096, 1, 0       /* 3–4 GiB   VBox LFB ~0xE0000000, QEMU 0xFD000000 */

/* ── BSS: kernel stack ──────────────────────────────────────────────────── */
.section .bss
.align 16
stack_bottom:
    .skip 32768
stack_top:

.section .note.GNU-stack, "", @progbits
