/*
 * CLOCK.S - PIT (Programmable Interval Timer) IRQ0 Handler
 * Architecture : x86 (32-bit)
 * Assembler    : GAS (AT&T syntax)
 *
 * The PIT fires IRQ0 at a configurable frequency (default ~18.2 Hz).
 * This handler:
 *   1. Saves the full CPU state (pusha + segment registers)
 *   2. Increments a global tick counter
 *   3. Sends an End-Of-Interrupt (EOI) to the PIC
 *   4. Restores state and returns with iret
 *
 * To link against C code, declare in your header:
 *   extern void clock_handler(void);
 *   extern volatile uint32_t clock_ticks;
 */

.section .data

.global clock_ticks
clock_ticks:
    .long 0                     /* 32-bit tick counter (~49-day rollover at 1 kHz) */

/* ------------------------------------------------------------------ */
.section .text

.global clock_handler
.type   clock_handler, @function

clock_handler:
    /* ── 1. Save state ─────────────────────────────────────────── */
    pusha                       /* EAX ECX EDX EBX ESP EBP ESI EDI  */

    push    %ds
    push    %es
    push    %fs
    push    %gs

    /* Load kernel data segment into all data segment registers */
    mov     $0x10, %ax          /* 0x10 = kernel data segment selector */
    mov     %ax,   %ds
    mov     %ax,   %es
    mov     %ax,   %fs
    mov     %ax,   %gs

    /* ── 2. Increment tick counter ─────────────────────────────── */
    incl    clock_ticks

    /* ── 3. (Optional) Call a higher-level C scheduler / handler ── */
    /*
     * Uncomment the lines below if you have a C function to call
     * on every tick, e.g. to update the scheduler:
     *
     *   call    timer_tick          // extern void timer_tick(void);
     */

    /* ── 4. Send EOI to the master PIC (port 0x20) ─────────────── */
    mov     $0x20, %al
    outb    %al,   $0x20

    /* ── 5. Restore state ──────────────────────────────────────── */
    pop     %gs
    pop     %fs
    pop     %es
    pop     %ds

    popa
    iret                        /* restore EIP, CS, EFLAGS (and SS:ESP if CPL changed) */

.size clock_handler, . - clock_handler


/* ==================================================================
 * pit_init  –  Program the PIT to fire at `hz` interrupts/second.
 *
 * C prototype:  void pit_init(uint32_t hz);
 *
 * PIT base frequency : 1,193,182 Hz
 * Divisor formula    : divisor = 1193182 / hz
 * Ports:
 *   0x43  Command register  (write only)
 *   0x40  Channel 0 data    (read/write)
 * ==================================================================*/

.global pit_init
.type   pit_init, @function

pit_init:
    push    %ebp
    mov     %esp,  %ebp

    /* ── Compute divisor = 1193182 / hz ────────────────────────── */
    mov     $1193182, %eax
    mov     8(%ebp),  %ecx     /* first argument: hz */
    xor     %edx,     %edx
    div     %ecx               /* EAX = quotient (divisor), EDX = remainder */

    /* ── Send command byte to PIT ──────────────────────────────── */
    /*
     * Command byte 0x36:
     *   Bits 7-6 : 00  → Select channel 0
     *   Bits 5-4 : 11  → Access mode: lobyte/hibyte
     *   Bits 3-1 : 011 → Mode 3 (square wave generator)
     *   Bit  0   : 0   → Binary (not BCD)
     */
    push    %eax                /* save divisor */
    mov     $0x36, %al
    outb    %al,   $0x43

    /* ── Send divisor (low byte then high byte) ─────────────────── */
    pop     %eax
    outb    %al,   $0x40       /* low byte  */
    mov     %ah,   %al
    outb    %al,   $0x40       /* high byte */

    pop     %ebp
    ret

.size pit_init, . - pit_init
