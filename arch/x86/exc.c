/**
 * arch/x86/exc.c — x86 32-bit exception handler
 *
 * Called from int80.S exc_common with a pointer to exc_frame_t.
 *
 * Stack layout built by exc_common (low addr = top):
 *   After pushal, the frame pointer (ESP) points to:
 *     +0  edi      \
 *     +4  esi       |
 *     +8  ebp       |  pushal (32 bytes)
 *    +12  esp_snap  |
 *    +16  ebx       |
 *    +20  edx       |
 *    +24  ecx       |
 *    +28  eax      /
 *    +32  cr2      (xchgl trick saves it here)
 *    +36  vec
 *    +40  err
 *    +44  eip      \
 *    +48  cs        |  CPU iret frame
 *    +52  eflags   /
 */

#include "ark/types.h"
#include "ark/printk.h"

typedef struct {
    u32 edi, esi, ebp, esp_snap, ebx, edx, ecx, eax; /* pushal +0..+28 */
    u32 cr2;     /* +32 */
    u32 vec;     /* +36 */
    u32 err;     /* +40 */
    u32 eip;     /* +44 */
    u32 cs;      /* +48 */
    u32 eflags;  /* +52 */
} __attribute__((packed)) exc_frame_t;

static const char *exc_name(u32 v) {
    static const char *t[] = {
        "#DE Divide Error", "#DB Debug", "NMI", "#BP Breakpoint",
        "#OF Overflow",     "#BR Bound", "#UD Bad Opcode", "#NM No FPU",
        "#DF Double Fault", "Coproc",    "#TS Bad TSS",   "#NP Not Present",
        "#SS Stack Fault",  "#GP General Protection", "#PF Page Fault", "(15)",
        "#MF x87 FP",       "#AC Align", "#MC Machine Check", "#XM SIMD FP",
    };
    return (v < 20) ? t[v] : "Unknown";
}

static void backtrace(u32 ebp, u32 eip) {
    printk("Backtrace:\n  [0] 0x%08x\n", eip);
    for (int i = 1; i < 12; i++) {
        if (!ebp || ebp < 0x100000u || (ebp & 3)) break;
        u32 *fp = (u32 *)ebp;
        u32 ret = fp[1];
        if (!ret) break;
        printk("  [%d] 0x%08x\n", i, ret);
        if (fp[0] <= ebp) break;
        ebp = fp[0];
    }
}

__attribute__((noreturn))
void exception_handler_c(exc_frame_t *f)
{
    __asm__ volatile("cli");

    printk("\n=== EXCEPTION %u: %s ===\n", f->vec, exc_name(f->vec));
    printk("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n", f->eax,f->ebx,f->ecx,f->edx);
    printk("ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n", f->esi,f->edi,f->ebp,f->esp_snap);
    printk("EIP=%08x CS=%08x EFLAGS=%08x\n",        f->eip,f->cs,f->eflags);

    if (f->vec == 14) {
        printk("CR2=%08x [%s %s %s]\n", f->cr2,
               (f->err & 1) ? "prot" : "not-present",
               (f->err & 2) ? "write" : "read",
               (f->err & 4) ? "user" : "kernel");
    } else if (f->err) {
        printk("Err=0x%08x\n", f->err);
    }

    backtrace(f->ebp, f->eip);
    printk("System halted.\n");
    for (;;) __asm__ volatile("hlt");
    __builtin_unreachable();
}
