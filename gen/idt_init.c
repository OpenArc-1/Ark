/**
 * gen/idt_init.c — IDT initialisation for both x86 (32-bit) and x86_64
 *
 * 32-bit: Builds a full 256-entry IDT with stub handlers for all CPU
 *         exceptions (0-31), PIC remap, IRQ handlers, int 0x80 syscall gate.
 *
 * 64-bit: idt64_init() was already called from arch_x86_64_entry() in
 *         arch/x86_64/built-in.c before kernel_main().  This file's
 *         idt_init() is therefore a deliberate no-op on x86_64.
 *
 * Guard: ARK_BITS is always set to 32 or 64 by the Makefile via -DARK_BITS=.
 * Using ARK_BITS instead of CONFIG_64BIT makes the guard robust against any
 * kconfig that might forget to set CONFIG_64BIT.
 */

#include "ark/types.h"
#include "ark/printk.h"

/* ══════════════════════════════════════════════════════════════════════════
 * 64-bit path — no-op (idt64_init already ran from arch_x86_64_entry)
 * ══════════════════════════════════════════════════════════════════════════ */
#if ARK_BITS == 64

void idt_init(void) { /* idt64_init() already called in arch_x86_64_entry() */ }

/* ══════════════════════════════════════════════════════════════════════════
 * 32-bit path
 * ══════════════════════════════════════════════════════════════════════════ */
#else

typedef struct {
    u16 offset_low;
    u16 selector;
    u8  reserved;
    u8  type_attr;
    u16 offset_high;
} __attribute__((packed)) idt32_entry_t;

typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) idt32_desc_t;

extern idt32_entry_t g_idt[];
extern idt32_desc_t  g_idt_desc;
extern void          int80_syscall_stub(void);

static void idt32_set_gate(u8 vector, void (*handler)(void),
                           u16 selector, u8 type_attr) {
    u32 addr = (u32)handler;
    g_idt[vector].offset_low  = (u16)(addr & 0xFFFF);
    g_idt[vector].offset_high = (u16)((addr >> 16) & 0xFFFF);
    g_idt[vector].selector    = selector;
    g_idt[vector].reserved    = 0;
    g_idt[vector].type_attr   = type_attr;
}

extern void isr_stub_de(void);
extern void isr_stub_db(void);
extern void isr_stub_nmi(void);
extern void isr_stub_bp(void);
extern void isr_stub_of(void);
extern void isr_stub_br(void);
extern void isr_stub_ud(void);
extern void isr_stub_nm(void);
extern void isr_stub_df(void);
extern void isr_stub_9_(void);   /* coprocessor overrun (legacy, vec 9) */
extern void isr_stub_ts(void);
extern void isr_stub_np(void);
extern void isr_stub_ss(void);
extern void isr_stub_gp(void);
extern void isr_stub_pf(void);
extern void isr_stub_mf(void);
extern void isr_stub_ac(void);
extern void isr_stub_mc(void);
extern void isr_stub_xm(void);
extern void irq0_handler(void);
extern void irq1_handler(void);
extern void irq_spurious_2(void);
extern void irq_spurious_3(void);
extern void irq_spurious_4(void);
extern void irq_spurious_5(void);
extern void irq_spurious_6(void);
extern void irq_spurious_7(void);
extern void irq_spurious_8(void);
extern void irq_spurious_9(void);
extern void irq_spurious_10(void);
extern void irq_spurious_11(void);
extern void irq_spurious_12(void);
extern void irq_spurious_13(void);
extern void irq_spurious_14(void);
extern void irq_spurious_15(void);

#define GATE_INT_KERN  0x8E
#define GATE_INT_USER  0xEE
#define KERN_CS        0x08

void idt_init(void) {
    u8 *p = (u8 *)g_idt;
    for (u32 i = 0; i < 256 * sizeof(idt32_entry_t); i++) p[i] = 0;

    idt32_set_gate(0,  isr_stub_de,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(1,  isr_stub_db,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(2,  isr_stub_nmi, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(3,  isr_stub_bp,  KERN_CS, GATE_INT_USER);
    idt32_set_gate(4,  isr_stub_of,  KERN_CS, GATE_INT_USER);
    idt32_set_gate(5,  isr_stub_br,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(6,  isr_stub_ud,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(7,  isr_stub_nm,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(8,  isr_stub_df,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(9,  isr_stub_9_,  KERN_CS, GATE_INT_KERN);  /* coprocessor overrun */
    idt32_set_gate(10, isr_stub_ts,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(11, isr_stub_np,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(12, isr_stub_ss,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(13, isr_stub_gp,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(14, isr_stub_pf,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(16, isr_stub_mf,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(17, isr_stub_ac,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(18, isr_stub_mc,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(19, isr_stub_xm,  KERN_CS, GATE_INT_KERN);

    /* Remap 8259 PIC: master→0x20, slave→0x28 */
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x11),"Nd"((u16)0x20));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x11),"Nd"((u16)0xA0));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x20),"Nd"((u16)0x21));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x28),"Nd"((u16)0xA1));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x04),"Nd"((u16)0x21));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x02),"Nd"((u16)0xA1));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x01),"Nd"((u16)0x21));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0x01),"Nd"((u16)0xA1));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0xFC),"Nd"((u16)0x21));
    __asm__ __volatile__("outb %b0,%w1"::"a"((u8)0xFF),"Nd"((u16)0xA1));

    idt32_set_gate(0x20, irq0_handler,    KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x21, irq1_handler,    KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x22, irq_spurious_2,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x23, irq_spurious_3,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x24, irq_spurious_4,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x25, irq_spurious_5,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x26, irq_spurious_6,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x27, irq_spurious_7,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x28, irq_spurious_8,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x29, irq_spurious_9,  KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x2A, irq_spurious_10, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x2B, irq_spurious_11, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x2C, irq_spurious_12, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x2D, irq_spurious_13, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x2E, irq_spurious_14, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x2F, irq_spurious_15, KERN_CS, GATE_INT_KERN);
    idt32_set_gate(0x80, int80_syscall_stub, KERN_CS, GATE_INT_USER);

    __asm__ __volatile__("lidt %0"::"m"(g_idt_desc));
    __asm__ __volatile__("sti");

    printk(T, "idt: PIC remapped, IRQ0/IRQ1 active, interrupts enabled\n");
}

#endif /* ARK_BITS */
