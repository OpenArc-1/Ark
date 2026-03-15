/**
 * arch/x86/int80.S — x86 32-bit interrupt stubs + IDT data
 */
.code32
.section .text
.align 4

/* ── Exception stub helpers ────────────────────────────────────────
 *
 * All stubs converge on exc_common which builds a uniform exc_frame_t
 * on the stack and calls exception_handler_c(exc_frame_t *).
 *
 * exc_frame_t layout (address increases downward, ESP points to top):
 *   +0   vec      u32  — exception vector
 *   +4   err      u32  — error code (0 for no-error exceptions)
 *   +8   cr2      u32  — CR2 (faulting address, only valid for #PF)
 *   +12  edi      u32  \
 *   +16  esi      u32   |
 *   +20  ebp      u32   | pushal frame
 *   +24  esp_snap u32   |  (ESP value before pushal)
 *   +28  ebx      u32   |
 *   +32  edx      u32   |
 *   +36  ecx      u32   |
 *   +40  eax      u32  /
 *   +44  eip      u32  \
 *   +48  cs       u32   | CPU iret frame
 *   +52  eflags   u32  /
 */

.extern exception_handler_c

/* Stubs for exceptions that do NOT push an error code */
.macro ISR_STUB name, vec
.globl isr_stub_\name
.type  isr_stub_\name, @function
isr_stub_\name:
    pushl $0          /* fake error code */
    pushl $\vec
    jmp   exc_common
.endm

/* Stubs for exceptions that DO push an error code (CPU pushed it before EIP) */
.macro ISR_STUB_ERR name, vec
.globl isr_stub_\name
.type  isr_stub_\name, @function
isr_stub_\name:
    /* Stack: [esp+0]=error_code  [esp+4]=eip  [esp+8]=cs  [esp+12]=eflags */
    pushl $\vec
    /* Now:   [esp+0]=vec  [esp+4]=error_code  [esp+8]=eip … */
    jmp   exc_common
.endm

/* ── CPU exception stubs ───────────────────────────────────────────── */
ISR_STUB     de,  0    /* #DE divide error              */
ISR_STUB     db,  1    /* #DB debug                     */
ISR_STUB     nmi, 2    /* NMI                           */
ISR_STUB     bp,  3    /* #BP breakpoint                */
ISR_STUB     of,  4    /* #OF overflow                  */
ISR_STUB     br,  5    /* #BR bound range               */
ISR_STUB     ud,  6    /* #UD invalid opcode            */
ISR_STUB     nm,  7    /* #NM device not available      */
ISR_STUB_ERR df,  8    /* #DF double fault  (ec always 0) */
ISR_STUB     9_,  9    /* coprocessor overrun (legacy)  */
ISR_STUB_ERR ts,  10   /* #TS invalid TSS               */
ISR_STUB_ERR np,  11   /* #NP segment not present       */
ISR_STUB_ERR ss,  12   /* #SS stack fault               */
ISR_STUB_ERR gp,  13   /* #GP general protection        */
ISR_STUB_ERR pf,  14   /* #PF page fault                */
ISR_STUB     mf,  16   /* #MF x87 FP                    */
ISR_STUB_ERR ac,  17   /* #AC alignment check           */
ISR_STUB     mc,  18   /* #MC machine check             */
ISR_STUB     xm,  19   /* #XM SIMD FP                   */

/* ── Common exception path ─────────────────────────────────────────── */
exc_common:
    /* Stack on entry:
     *   [esp+0]  vec
     *   [esp+4]  err       (0 for no-error stubs, CPU value for err stubs)
     *   [esp+8]  eip    \
     *   [esp+12] cs      > CPU iret frame
     *   [esp+16] eflags /
     *
     * Target exc_frame_t layout (must match exc.c exactly):
     *   [esp+0]  edi      \
     *   [esp+4]  esi       |
     *   [esp+8]  ebp       | pushal  (32 bytes)
     *   [esp+12] esp_snap  |
     *   [esp+16] ebx       |
     *   [esp+20] edx       |
     *   [esp+24] ecx       |
     *   [esp+28] eax      /
     *   [esp+32] cr2       ← we insert this
     *   [esp+36] vec
     *   [esp+40] err
     *   [esp+44] eip
     *   [esp+48] cs
     *   [esp+52] eflags
     *
     * Strategy: reserve a cr2 slot BEFORE pushal so pushal lands at [esp+0].
     * Then fill cr2 after pushal (CR2 is only clobbered by a page fault
     * within this handler, which cannot happen since we have no handler yet).
     */
    subl  $4, %esp           /* open slot for cr2 at future [esp+32] */
    pushal                   /* edi..eax at [esp+0..+28]; cr2 slot at [esp+32] */

    /* Now fill the cr2 slot */
    movl  %cr2, %eax
    movl  %eax, 32(%esp)    /* cr2 → [esp+32] */

    /* Restore eax from the pushal frame (we clobbered it above) */
    movl  28(%esp), %eax

    /* Stack is now exactly exc_frame_t — pass pointer to C handler */
    pushl %esp
    call  exception_handler_c
    /* Never returns */
    addl  $4, %esp
    cli
9999:
    hlt
    jmp 9999b

/* ── IRQ handlers ─────────────────────────────────────────────────
 * IRQ0 = vector 0x20 (timer)   — increments tick, sends EOI
 * IRQ1 = vector 0x21 (keyboard)— sets kbd_irq_pending flag, sends EOI
 *        (kbd_poll is called from the C polling loop, not here directly,
 *         to avoid stack/alignment issues in an interrupt context)
 * All other IRQs = spurious, just send EOI
 * ────────────────────────────────────────────────────────────────── */

.section .data
.globl kbd_irq_pending
kbd_irq_pending:
    .byte 0

.section .text

.extern sched_irq_tick

.globl irq0_handler
.type  irq0_handler, @function
irq0_handler:
    pushal                      /* save EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI (32 bytes) */

    /* EOI to master PIC */
    movb  $0x20, %al
    outb  %al,   $0x20

    /* Capture the frame pointer NOW (before we touch the stack further).
     * ESP currently points to the bottom of the pushal frame (EDI slot).
     * We pass this value to sched_irq_tick so it can save/restore it. */
    movl  %esp, %ebx            /* EBX = frame pointer (pushal frame top) */
    pushl %ebx                  /* push as arg to sched_irq_tick */
    call  sched_irq_tick        /* returns new frame pointer in EAX */
    addl  $4, %esp              /* pop arg — but DON'T use ESP yet */

    /* Set ESP to the returned frame pointer (same or switched task) */
    movl  %eax, %esp

    popal                       /* restore all GP regs from frame */
    iret                        /* pop EIP/CS/EFLAGS → resume task */

.globl irq1_handler
.type  irq1_handler, @function
irq1_handler:
    pushal
    /* Set pending flag — kbd_poll() called from C polling loop */
    movb  $1, kbd_irq_pending
    /* EOI to master PIC */
    movb  $0x20, %al
    outb  %al,   $0x20
    popal
    iret

/* Spurious IRQ stub — just EOI and return */
.macro IRQ_SPURIOUS name, slave
.globl irq_spurious_\name
.type  irq_spurious_\name, @function
irq_spurious_\name:
    pushal
    .if \slave
    movb $0x20, %al; outb %al, $0xA0   /* EOI slave  */
    .endif
    movb $0x20, %al; outb %al, $0x20   /* EOI master */
    popal
    iret
.endm

IRQ_SPURIOUS 2,  0
IRQ_SPURIOUS 3,  0
IRQ_SPURIOUS 4,  0
IRQ_SPURIOUS 5,  0
IRQ_SPURIOUS 6,  0
IRQ_SPURIOUS 7,  0
IRQ_SPURIOUS 8,  1
IRQ_SPURIOUS 9,  1
IRQ_SPURIOUS 10, 1
IRQ_SPURIOUS 11, 1
IRQ_SPURIOUS 12, 1
IRQ_SPURIOUS 13, 1
IRQ_SPURIOUS 14, 1
IRQ_SPURIOUS 15, 1

/* ── int 0x80 syscall handler ────────────────────────────────────── */
.extern syscall_dispatch

.globl int80_syscall_stub
.type  int80_syscall_stub, @function
int80_syscall_stub:
    pushal                  /* save all GP regs (32 bytes) */
    /* After pushal, relative to new esp:
     *   esp+0=edi  esp+4=esi   esp+8=ebp  esp+12=esp_saved
     *   esp+16=ebx esp+20=edx  esp+24=ecx esp+28=eax
     *
     * syscall_dispatch(number, arg1, arg2, arg3)
     *   number = eax,  arg1 = ebx,  arg2 = ecx,  arg3 = edx
     *
     * cdecl: push args right-to-left (arg3 first, number last).
     * Each push decrements esp by 4, so offsets increase by 4 each time.
     *
     * Push 1 (arg3=edx):  edx at [esp+20]  → pushl 20(%esp)
     * Push 2 (arg2=ecx):  ecx shifted to [esp+28] → pushl 28(%esp)
     * Push 3 (arg1=ebx):  ebx shifted to [esp+24] → pushl 24(%esp)
     * Push 4 (number=eax):eax shifted to [esp+40] → pushl 40(%esp)
     */
    pushl 20(%esp)          /* arg3 = edx  (esp shifts: edx was at +20) */
    pushl 28(%esp)          /* arg2 = ecx  (ecx was at +24, now +28)    */
    pushl 24(%esp)          /* arg1 = ebx  (ebx was at +16, now +24)    */
    pushl 40(%esp)          /* number= eax (eax was at +28, now +40)    */
    call  syscall_dispatch
    addl  $16, %esp         /* pop the 4 pushed args */
    movl  %eax, 28(%esp)    /* patch return value into saved eax slot */
    popal
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
