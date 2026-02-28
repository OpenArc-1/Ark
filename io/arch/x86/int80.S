/**
 * arch/x86/int80.S — x86 32-bit interrupt stubs + IDT data
 */
.code32
.section .text
.align 4

/* ── Exception stub helper ─────────────────────────────────────────
 * Sends one byte to COM1 port 0x3F8, using DX register (no immediate
 * port issues in 32-bit mode). Then halts forever.
 */
.macro ISR_STUB name, letter
.globl isr_stub_\name
.type  isr_stub_\name, @function
isr_stub_\name:
    pushal
    /* Wait for COM1 TX ready: LSR (0x3F8+5=0x3FD) bit 5 */
    movw  $0x3FD, %dx
8881:
    inb   %dx, %al
    testb $0x20, %al
    jz    8881b
    /* Send letter */
    movw  $0x3F8, %dx
    movb  $\letter, %al
    outb  %al, %dx
    /* Send newline */
    movw  $0x3FD, %dx
8882:
    inb   %dx, %al
    testb $0x20, %al
    jz    8882b
    movw  $0x3F8, %dx
    movb  $0x0A, %al
    outb  %al, %dx
8883:
    hlt
    jmp  8883b
.endm

/* CPU exceptions 0-19 */
ISR_STUB de,  0x44    /* D - Divide Error          */
ISR_STUB db,  0x47    /* G - Debug                 */
ISR_STUB nmi, 0x4E    /* N - NMI                   */
ISR_STUB bp,  0x42    /* B - Breakpoint            */
ISR_STUB of,  0x4F    /* O - Overflow              */
ISR_STUB br,  0x52    /* R - Bound Range           */
ISR_STUB ud,  0x55    /* U - Undefined Opcode      */
ISR_STUB nm,  0x4D    /* M - Device Not Available  */
ISR_STUB df,  0x46    /* F - Double Fault          */
ISR_STUB ts,  0x54    /* T - Invalid TSS           */
ISR_STUB np,  0x50    /* P - Not Present           */
ISR_STUB ss,  0x53    /* S - Stack Fault           */
ISR_STUB gp,  0x47    /* G - General Protection    */
ISR_STUB pf,  0x4C    /* L - Page Fault            */
ISR_STUB mf,  0x58    /* X - x87 FP                */
ISR_STUB ac,  0x41    /* A - Alignment Check       */
ISR_STUB mc,  0x43    /* C - Machine Check         */
ISR_STUB xm,  0x49    /* I - SIMD FP               */

/* ── int 0x80 syscall handler ────────────────────────────────────── */
.extern syscall_dispatch

.globl int80_syscall_stub
.type  int80_syscall_stub, @function
int80_syscall_stub:
    pushl %ebp
    pushl %esi
    pushl %edi
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    call  syscall_dispatch
    addl  $16, %esp
    popl  %edi
    popl  %esi
    popl  %ebp
    iret

/* ── ELF userspace launcher ──────────────────────────────────────── */
.globl run_elf_entry
.type  run_elf_entry, @function
run_elf_entry:
    movl 4(%esp), %eax
    movl 8(%esp), %esp
    jmp  *%eax

/* ── IDT storage ─────────────────────────────────────────────────── */
.section .data
.align 8

.globl g_idt
.globl g_idt_desc
g_idt:
    .space 2048

g_idt_desc:
    .word 2047
    .long g_idt
