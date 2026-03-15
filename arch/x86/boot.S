/* arch/x86/boot.S — 32-bit protected-mode entry for Ark kernel
 *
 * Sets up the kernel's own GDT (flat 4 GiB code + data segments),
 * reloads all segment registers, establishes a known-good stack,
 * then jumps to arch_x86_entry(magic, mb_info).
 *
 * Having our own GDT means we never depend on GRUB's descriptor table
 * staying valid after boot, and our IDT can safely reference 0x08/0x10.
 */

.code32
.section .text
.align 4

.globl _start
.type  _start, @function
_start:
    /* Disable interrupts until our IDT is live */
    cli

    /* Save multiboot regs before we clobber anything */
    movl %eax, %edi     /* magic      */
    movl %ebx, %esi     /* mb_info ptr */

    /* Load our own GDT */
    lgdt gdt_ptr

    /* Reload CS via a far jump */
    ljmp $0x08, $.Lflush_cs
.Lflush_cs:

    /* Reload all data segment registers to our flat data selector */
    movw $0x10, %ax
    movw %ax,   %ds
    movw %ax,   %es
    movw %ax,   %fs
    movw %ax,   %gs
    movw %ax,   %ss

    /* Set up kernel stack */
    leal stack_top, %esp
    xorl %ebp, %ebp

    /* Call C entry: arch_x86_entry(magic, mb_info) */
    pushl %esi      /* arg2: mb_info */
    pushl %edi      /* arg1: magic   */
    call  arch_x86_entry

.Lhalt:
    cli
    hlt
    jmp .Lhalt

/* ── GDT ────────────────────────────────────────────────────────────────── */
.section .data
.align 8

gdt:
    /* 0x00: null descriptor */
    .quad 0x0000000000000000

    /* 0x08: kernel code — base=0, limit=4GiB, 32-bit, ring 0, execute/read */
    .quad 0x00CF9A000000FFFF

    /* 0x10: kernel data — base=0, limit=4GiB, 32-bit, ring 0, read/write */
    .quad 0x00CF92000000FFFF

    /* 0x18: user code — base=0, limit=4GiB, 32-bit, ring 3, execute/read */
    .quad 0x00CFFA000000FFFF

    /* 0x20: user data — base=0, limit=4GiB, 32-bit, ring 3, read/write */
    .quad 0x00CFF2000000FFFF

gdt_ptr:
    .word gdt_ptr - gdt - 1     /* limit = size - 1 */
    .long gdt                   /* base  */

/* ── Stack ──────────────────────────────────────────────────────────────── */
.section .bss
.align 16
stack_bottom:
    .skip 32768         /* 32 KiB — bumped from 16 KiB for IRQ stack safety */
stack_top:

/* Mark stack non-executable */
.section .note.GNU-stack,"",@progbits
