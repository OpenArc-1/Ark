/*
 * arch/x86_64/pit.S — 8253/8254 PIT initialisation for x86_64
 *
 * void pit_init(uint32_t hz);
 *
 * Programs PIT channel 0 in mode 3 (square-wave) at the requested
 * frequency.  The PIT I/O ports are 8-bit so outb is used regardless
 * of the host address width.
 *
 *  x86_64 C ABI: first argument in %edi (32-bit view of %rdi).
 */

.section .text
.code64

.globl pit_init
.type  pit_init, @function
pit_init:
    /* Compute divisor = 1193182 / hz (hz is in edi) */
    movl    $1193182, %eax
    movl    %edi,     %ecx
    testl   %ecx, %ecx
    jz      .Lpit_done          /* guard against hz=0 */
    xorl    %edx,     %edx
    divl    %ecx                /* eax = divisor */

    /*
     * Command byte 0x36:
     *   Bits 7-6 : 00  → channel 0
     *   Bits 5-4 : 11  → lobyte/hibyte access
     *   Bits 3-1 : 011 → mode 3 (square wave)
     *   Bit  0   : 0   → binary
     */
    movl    %eax,  %ebx         /* save divisor */
    movb    $0x36, %al
    outb    %al,   $0x43        /* PIT command port */

    movl    %ebx,  %eax
    outb    %al,   $0x40        /* low byte  → channel 0 data */
    movb    %ah,   %al
    outb    %al,   $0x40        /* high byte → channel 0 data */

.Lpit_done:
    ret
.size pit_init, . - pit_init

.section .note.GNU-stack, "", @progbits
