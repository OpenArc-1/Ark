/**
 * gen/idt_init.c — IDT initialisation for both x86 (32-bit) and x86_64
 *
 * 32-bit: Builds a full 256-entry IDT with stub handlers for all CPU
 *         exceptions (0-31) so faults don't cause silent triple-faults/resets,
 *         plus installs int 0x80 syscall gate.
 *
 * 64-bit: Delegates to idt64_init() in arch/x86_64/idt.S.
 */

#include "ark/types.h"
#include "ark/printk.h"

/* ══════════════════════════════════════════════════════════════════════════
 * 64-bit path
 * ══════════════════════════════════════════════════════════════════════════ */
#if defined(CONFIG_64BIT) && CONFIG_64BIT

extern void idt64_init(void);

void idt_init(void) {
    /* idt64_init() was already called from arch_x86_64_entry() in built-in.c.
     * This second call from kernel_main() is a no-op but safe. */
}

/* ══════════════════════════════════════════════════════════════════════════
 * 32-bit path
 * ══════════════════════════════════════════════════════════════════════════ */
#else

/* ── IDT entry structure (32-bit interrupt gate) ── */
typedef struct {
    u16 offset_low;   /* handler addr bits 0-15  */
    u16 selector;     /* code segment selector    */
    u8  reserved;     /* always 0                 */
    u8  type_attr;    /* gate type + DPL + present */
    u16 offset_high;  /* handler addr bits 16-31  */
} __attribute__((packed)) idt32_entry_t;

typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) idt32_desc_t;

/* Defined in arch/x86/int80.S */
extern idt32_entry_t g_idt[];
extern idt32_desc_t  g_idt_desc;
extern void          int80_syscall_stub(void);

/* ── Install one 32-bit interrupt gate ── */
static void idt32_set_gate(u8 vector, void (*handler)(void),
                           u16 selector, u8 type_attr) {
    u32 addr = (u32)handler;
    g_idt[vector].offset_low  = (u16)(addr & 0xFFFF);
    g_idt[vector].offset_high = (u16)((addr >> 16) & 0xFFFF);
    g_idt[vector].selector    = selector;
    g_idt[vector].reserved    = 0;
    g_idt[vector].type_attr   = type_attr;
}

/*
 * Minimal exception stubs — declared in arch/x86/int80.S
 * We declare them as void(void) and cast; they are iret stubs.
 */
extern void isr_stub_de(void);   /* 0  #DE Divide Error        */
extern void isr_stub_db(void);   /* 1  #DB Debug               */
extern void isr_stub_nmi(void);  /* 2  NMI                     */
extern void isr_stub_bp(void);   /* 3  #BP Breakpoint          */
extern void isr_stub_of(void);   /* 4  #OF Overflow            */
extern void isr_stub_br(void);   /* 5  #BR Bound Range         */
extern void isr_stub_ud(void);   /* 6  #UD Invalid Opcode      */
extern void isr_stub_nm(void);   /* 7  #NM Device Not Available */
extern void isr_stub_df(void);   /* 8  #DF Double Fault        */
extern void isr_stub_ts(void);   /* 10 #TS Invalid TSS         */
extern void isr_stub_np(void);   /* 11 #NP Seg Not Present     */
extern void isr_stub_ss(void);   /* 12 #SS Stack Fault         */
extern void isr_stub_gp(void);   /* 13 #GP General Protection  */
extern void isr_stub_pf(void);   /* 14 #PF Page Fault          */
extern void isr_stub_mf(void);   /* 16 #MF x87 FP Exception    */
extern void isr_stub_ac(void);   /* 17 #AC Alignment Check     */
extern void isr_stub_mc(void);   /* 18 #MC Machine Check       */
extern void isr_stub_xm(void);   /* 19 #XM SIMD FP Exception   */

#define GATE_INT_KERN  0x8E   /* present, ring 0, 32-bit interrupt gate */
#define GATE_INT_USER  0xEE   /* present, ring 3, 32-bit interrupt gate */
#define KERN_CS        0x08   /* kernel code segment selector */

void idt_init(void) {
    /* Zero entire IDT first */
    u8 *p = (u8 *)g_idt;
    for (u32 i = 0; i < 256 * sizeof(idt32_entry_t); i++) p[i] = 0;

    /* CPU exception handlers (vectors 0-19) */
    idt32_set_gate(0,  isr_stub_de,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(1,  isr_stub_db,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(2,  isr_stub_nmi, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(3,  isr_stub_bp,  KERN_CS, GATE_INT_USER);
    idt32_set_gate(4,  isr_stub_of,  KERN_CS, GATE_INT_USER);
    idt32_set_gate(5,  isr_stub_br,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(6,  isr_stub_ud,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(7,  isr_stub_nm,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(8,  isr_stub_df,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(10, isr_stub_ts,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(11, isr_stub_np,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(12, isr_stub_ss,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(13, isr_stub_gp,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(14, isr_stub_pf,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(16, isr_stub_mf,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(17, isr_stub_ac,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(18, isr_stub_mc,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(19, isr_stub_xm,  KERN_CS, GATE_INT_KERN);

    /* int 0x80 syscall gate (ring 3 accessible) */
    idt32_set_gate(0x80, int80_syscall_stub, KERN_CS, GATE_INT_USER);

    /* Load the IDT */
    __asm__ __volatile__("lidt %0" :: "m"(g_idt_desc));

    printk("IDT: 32-bit IDT loaded (exceptions 0-19, int 0x80 ready)\n");
}

#endif /* CONFIG_64BIT */
