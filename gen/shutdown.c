/*
 * gen/shutdown.c — System shutdown / reboot
 *
 * Shutdown sequence:
 *   1. ACPI S5 via PM1a_CNT register (works on real hardware + most VMs)
 *   2. PS/2 keyboard controller reset (reboot fallback)
 *   3. Triple-fault reboot (last resort)
 *   4. Halt loop
 */

#include "ark/types.h"
#include "ark/printk.h"

static inline void outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ACPI power-off via PM1a Control Register */
static void shutdown_acpi(u32 pm1a_cnt, u16 slp_typ) {
    if (!pm1a_cnt) return;
    /* SLP_EN = bit 13, SLP_TYP = bits 10:12 */
    outw((u16)pm1a_cnt, slp_typ | (1u << 13));
}

/* PS/2 keyboard controller CPU reset line */
static void reboot_ps2(void) {
    /* Wait for KBC input buffer to be empty */
    u8 stat;
    u32 tries = 0;
    do {
        __asm__ volatile("inb $0x64, %0" : "=a"(stat));
    } while ((stat & 0x02) && ++tries < 0x100000);
    outb(0x64, 0xFE); /* pulse reset line */
}

/* Triple-fault reboot: load a null IDT then fire an interrupt */
static void reboot_triple_fault(void) {
    struct { u16 limit; u32 base; } __attribute__((packed)) idtr = { 0, 0 };
    __asm__ volatile("lidt %0\n\tint3" : : "m"(idtr));
}

void k_shutdown(u32 acpi_ptr, u16 s5_val) {
    printk(T, "shutdown: attempting ACPI power-off\n");
    shutdown_acpi(acpi_ptr, s5_val);

    /* ACPI didn't work — reboot instead */
    printk(T, "shutdown: ACPI failed, rebooting via PS/2 KBC\n");
    reboot_ps2();

    printk(T, "shutdown: PS/2 reset failed, triple-fault reboot\n");
    reboot_triple_fault();

    for (;;)
        __asm__ volatile("cli; hlt");
}
