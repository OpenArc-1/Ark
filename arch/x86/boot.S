/* Minimal 32-bit entry for Ark.
 *
 * QEMU/Multiboot jumps here (ELF entry = _start).
 * Multiboot header itself now lives in C in arch/x86/built-in.c,
 * in a .multiboot section that the linker script places first.
 */

.code32

    .section .text
    .globl _start
_start:
    /* Set up a temporary stack. */
    lea stack_top, %esp

    /* Pass Multiboot magic (EAX) and info pointer (EBX) to C. */
    push %ebx
    push %eax
    call arch_x86_entry

.Lhalt:
    cli
    hlt
    jmp .Lhalt

    .section .bss
    .align 16
stack_bottom:
    .skip 16384           /* 16 KiB stack */
stack_top:

