/*
 * arch/x86_64/boot.S — Ark kernel x86_64 entry point (PVH + Multiboot2)
 */

/* ── PVH ELF Note ──────────────────────────────────────────────────── */
.section .note.Xen, "a", @note
.align 4
.long  4
.long  4
.long  18
.ascii "Xen\0"
.long  pvh_start_xen

/* ── Multiboot2 Header ─────────────────────────────────────────────── */
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

/* ── 32-bit bootstrap ──────────────────────────────────────────────── */
.section .text.boot, "ax"
.code32

.globl pvh_start_xen
.type  pvh_start_xen, @function
pvh_start_xen:
    /* PVH: 32-bit protected mode, paging off. EAX/EBX are NOT multiboot. */
    xorl %eax, %eax
    xorl %ebx, %ebx
    /* fall through */

.globl _start
.type  _start, @function
_start:
    cli
    cld

    /* Save multiboot magic + info pointer before anything clobbers regs */
    movl %eax, saved_magic
    movl %ebx, saved_mb_info

    /* 1. Load GDT and reload all segment registers */
    lgdt gdt64_ptr
    ljmp $0x08, $.Lcs_reload
.Lcs_reload:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    /* 2. Zero PML4 + PDPT + PD (3 × 4096 = 12288 bytes) */
    movl $pml4, %edi
    xorl %eax,  %eax
    movl $3072, %ecx        /* 12288 / 4 dwords */
    rep  stosl

    /* 3. PML4[0] → PDPT  (present | writable) */
    movl $pdpt, %eax
    orl  $3,    %eax
    movl %eax,  pml4

    /* 4. PDPT[0] → PD   (present | writable) */
    movl $pd,   %eax
    orl  $3,    %eax
    movl %eax,  pdpt

    /* 5. Fill PD with 2 MiB huge-page entries covering 0–4 GiB */
    movl $pd,         %edi
    movl $0x00000083, %eax  /* P | RW | PS (huge page) */
    xorl %edx,        %edx
    movl $2048,       %ecx
.Lfill_pd:
    movl %eax,  (%edi)
    movl %edx, 4(%edi)
    addl $0x200000, %eax    /* next 2 MiB */
    adcl $0,        %edx
    addl $8,        %edi
    decl %ecx
    jnz  .Lfill_pd

    /* 6. Enable PAE */
    movl %cr4, %eax
    orl  $0x20, %eax
    movl %eax, %cr4

    /* 7. CR3 = physical address of PML4 */
    movl $pml4, %eax
    movl %eax,  %cr3

    /* 8. EFER.LME = 1  (MSR 0xC0000080, bit 8) */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x100, %eax
    wrmsr

    /* 9. Enable paging + protected mode */
    movl %cr0, %eax
    orl  $0x80000001, %eax
    movl %eax, %cr0

    /* 10. Far-jump into 64-bit code segment (selector 0x18) */
    ljmp $0x18, $.Lentry64

/* ── 64-bit long mode ──────────────────────────────────────────────── */
.code64
.Lentry64:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    leaq stack_top(%rip), %rsp
    xorq %rbp, %rbp

    /* arg1 (RDI) = magic,  arg2 (RSI) = mb_info physical address */
    movl saved_magic(%rip),   %edi
    movl saved_mb_info(%rip), %esi

    cld
    call arch_x86_64_entry

.Lhang:
    cli
    hlt
    jmp .Lhang

/* ── GDT ───────────────────────────────────────────────────────────── */
.section .data
.align 8
gdt64:
    .quad 0x0000000000000000    /* 0x00: null */
    .quad 0x00CF9A000000FFFF    /* 0x08: 32-bit code */
    .quad 0x00CF92000000FFFF    /* 0x10: 32-bit data */
    .quad 0x00AF9A000000FFFF    /* 0x18: 64-bit code  (L=1) */
    .quad 0x00AF92000000FFFF    /* 0x20: 64-bit data */
.Lgdt64_end:

gdt64_ptr:
    .word .Lgdt64_end - gdt64 - 1
    .long gdt64

saved_magic:   .long 0
saved_mb_info: .long 0

/* ── BSS ───────────────────────────────────────────────────────────── */
.section .bss
.align 4096
pml4:  .skip 4096
pdpt:  .skip 4096
pd:    .skip 16384

.align 16
stack_bottom:
    .skip 32768
stack_top:

.section .note.GNU-stack, "", @progbits
