/* arch/x86/exch.S — REMOVED (dead code, wrong calling convention)
 *
 * This file previously defined _exc_stub_N symbols that were never wired
 * into the IDT (idt_init.c registers isr_stub_* from int80.S).
 * It also called exception_handler_c with 13 individual arguments, but
 * exc.c declares it taking a single exc_frame_t* pointer — an ABI mismatch
 * that would crash on any exception.
 *
 * All exception handling is in arch/x86/int80.S + arch/x86/exc.c.
 */
.code32
.section .text
/* intentionally empty */

/* DEAD CODE BELOW — kept for reference only, never assembled */
.if 0
 * Problems with the original int80.S exception stubs
 * ──────────────────────────────────────────────────
 * 1. They send a single letter to COM1 and halt.  NO register state, no
 *    fault address, no error code.  Debugging a #PF or #GP on real hardware
 *    requires a serial port and a mental mapping from a single letter to an
 *    exception number.
 *
 * 2. #PF (page fault) does not read CR2 (faulting linear address).  CR2 is
 *    overwritten by any subsequent memory access so it must be saved FIRST.
 *
 * 3. Exceptions that push an error code (#DF, #TS, #NP, #SS, #GP, #PF, #AC)
 *    are handled identically to those that do not, so the error-code slot
 *    is never exposed to the C handler.
 *
 * 4. The IRET frame is not printed.  Stack pointer at fault time is unknown.
 *
 * New design
 * ──────────
 * Each stub builds a uniform cpu_state_t on the stack and calls a single
 * C handler: exception_handler(cpu_state_t *).  The handler prints the full
 * register set + faulting address + error code + a mini stack trace, then
 * halts.
 *
 * cpu_state_t layout (low address = top of stack after stub entry):
 *
 *   +  0   vec      (u32)   — exception vector number
 *   +  4   err_code (u32)   — error code (0 for exceptions that don't push one)
 *   +  8   edi      (u32)
 *   + 12   esi
 *   + 16   ebp
 *   + 20   esp_usr  (u32)   — ESP *before* the exception (from pushal)
 *   + 24   ebx
 *   + 28   edx
 *   + 32   ecx
 *   + 36   eax
 *   + 40   eip      (u32)   — from iret frame pushed by CPU
 *   + 44   cs       (u32)
 *   + 48   eflags   (u32)
 *   [+ 52  esp_old  (u32)   — only present on privilege change]
 *   [+ 56  ss_old   (u32)]
 *
 * The C handler signature:
 *   void exception_handler(u32 vec, u32 err, u32 edi, u32 esi, u32 ebp,
 *                          u32 esp, u32 ebx, u32 edx, u32 ecx, u32 eax,
 *                          u32 eip, u32 cs, u32 eflags);
 *
 * Implemented with __attribute__((regparm(0))) / cdecl — all args on stack.
 */

.code32
.section .text
.align 4

/* ── Macro: no error code ────────────────────────────────────────────────── */
.macro EXC_NOERR vec
.globl _exc_stub_\vec
.type  _exc_stub_\vec, @function
_exc_stub_\vec:
    pushl $0               /* fake error code — keep frame uniform */
    pushl $\vec
    jmp   exc_common
.endm

/* ── Macro: CPU pushes error code ────────────────────────────────────────── */
.macro EXC_HSERR vec
.globl _exc_stub_\vec
.type  _exc_stub_\vec, @function
_exc_stub_\vec:
    /* CPU already pushed error code on stack; we push vec on top of it.
     * Stack on entry (high→low):
     *   [esp+8]  eflags
     *   [esp+4]  cs
     *   [esp+0]  eip
     *   [esp-4]  error_code   ← already pushed by CPU
     *
     * After we push vec:
     *   [esp+0]  vec
     *   [esp+4]  error_code
     *   [esp+8]  eip …
     * → we swap them so layout is always: [esp+0]=vec [esp+4]=err
     */
    /* At entry: [esp+0]=eip  [esp+4]=cs  [esp+8]=eflags */
    /* CPU pushed error code BEFORE eip, so actual layout at entry to stub:
     *   [esp+0]  error_code   (pushed by CPU before transfer to our stub)
     *   [esp+4]  eip
     *   [esp+8]  cs
     *   [esp+12] eflags
     * Wait — for error-code exceptions the CPU pushes: eflags, cs, eip, error.
     * So at stub entry stack top = error_code, then eip, cs, eflags.
     */
    pushl $\vec            /* [esp] = vec; [esp+4] = error_code */
    jmp   exc_common
.endm

/* Exception stubs */
EXC_NOERR   0   /* #DE divide error         */
EXC_NOERR   1   /* #DB debug                */
EXC_NOERR   2   /* #NMI                     */
EXC_NOERR   3   /* #BP breakpoint           */
EXC_NOERR   4   /* #OF overflow             */
EXC_NOERR   5   /* #BR bound range          */
EXC_NOERR   6   /* #UD invalid opcode       */
EXC_NOERR   7   /* #NM device not available */
EXC_HSERR   8   /* #DF double fault (ec=0)  */
EXC_NOERR   9   /* coprocessor overrun      */
EXC_HSERR  10   /* #TS invalid TSS          */
EXC_HSERR  11   /* #NP not present          */
EXC_HSERR  12   /* #SS stack fault          */
EXC_HSERR  13   /* #GP general protection   */
EXC_HSERR  14   /* #PF page fault           */
EXC_NOERR  16   /* #MF x87 FP               */
EXC_HSERR  17   /* #AC alignment check      */
EXC_NOERR  18   /* #MC machine check        */
EXC_NOERR  19   /* #XM SIMD FP              */

/* ── Common path ─────────────────────────────────────────────────────────── */
.extern exception_handler_c

exc_common:
    /* Stack on entry:
     *   [esp+0]  vec
     *   [esp+4]  err_code
     *   [esp+8]  eip    \
     *   [esp+12] cs      > CPU iret frame
     *   [esp+16] eflags /
     */
    pushal          /* saves eax,ecx,edx,ebx,esp,ebp,esi,edi (32 bytes) */

    /* At this point esp points to:
     *   [esp+0]  edi
     *   [esp+4]  esi
     *   [esp+8]  ebp
     *   [esp+12] esp_before_pushal
     *   [esp+16] ebx
     *   [esp+20] edx
     *   [esp+24] ecx
     *   [esp+28] eax
     *   [esp+32] vec        ← pushed before pushal
     *   [esp+36] err_code
     *   [esp+40] eip
     *   [esp+44] cs
     *   [esp+48] eflags
     */

    /* Save CR2 (page fault address) into EAX — it is volatile so save now */
    movl  %cr2, %eax
    pushl %eax          /* CR2 on stack — extra arg to C handler */

    /* Pass the whole frame as pointer (cdecl — stack pointer = &cr2) */
    /* Actually we call exception_handler_c(vec, err, regs...) 
     * by pushing individual values for clarity */

    /* Load values from frame */
    movl 36(%esp), %eax   /* eax (was at esp+28 before cr2 push → +36) */
    movl 40(%esp), %ecx   /* ecx */
    movl 44(%esp), %edx   /* edx */
    movl 48(%esp), %ebx   /* ebx */
    movl 56(%esp), %ebp   /* ebp */
    movl 60(%esp), %esi   /* esi */
    movl 64(%esp), %edi   /* edi */

    /* Push args right-to-left (cdecl):
     * exception_handler_c(u32 vec, u32 err, u32 eip, u32 cs, u32 eflags,
     *                     u32 eax, u32 ecx, u32 edx, u32 ebx,
     *                     u32 ebp, u32 esi, u32 edi, u32 cr2) */

    pushl  0(%esp)         /* cr2 — already at top of stack */
    pushl  64+4(%esp)      /* edi (adjust for extra push) */
    pushl  60+8(%esp)      /* esi */
    pushl  56+12(%esp)     /* ebp */
    pushl  48+16(%esp)     /* ebx */
    pushl  44+20(%esp)     /* edx */
    pushl  40+24(%esp)     /* ecx */
    pushl  36+28(%esp)     /* eax */
    pushl  68+32(%esp)     /* eflags: was at [frame+48], now +68 + 32 pushes */
    pushl  64+36(%esp)     /* cs */
    pushl  60+40(%esp)     /* eip */
    pushl  68+44(%esp)     /* err_code: was [frame+36] */
    pushl  64+48(%esp)     /* vec: was [frame+32] */
    call   exception_handler_c
    /* We never return — handler halts */
    cli
1:  hlt
    jmp 1b
.endif /* never assembled */