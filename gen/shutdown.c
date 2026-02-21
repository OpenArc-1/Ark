#include <stdint.h>
#include "ark/printk.h"

// Basic I/O functions for x86 communication
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * METHOD 1: ACPI Shutdown (The "Proper" Way)
 * Note: You must parse the FADT table during boot to get these values.
 * If you haven't parsed ACPI yet, this method will be skipped.
 */
void shutdown_acpi(uint32_t PM1a_CNT, uint16_t SLP_TYP) {
    if (PM1a_CNT == 0) return;

    // SLP_EN is bit 13. We OR the Sleep Type (S5) with the Enable bit.
    // Standard ACPI Power Off sequence
    outw(PM1a_CNT, SLP_TYP | (1 << 13));
}

/**
 * METHOD 2: Emulator Specifics
 * Works in QEMU, Bochs, and VirtualBox.
 */
void shutdown_emulator() {
    // QEMU/Bochs older versions
    outw(0xB004, 0x2000);
    
    // QEMU newer versions (Standard debug exit)
    outw(0x604, 0x2000);
    
    // VirtualBox
    outw(0x4004, 0x3400);
}

/**
 * METHOD 3: The Triple Fault (Reboot Fallback)
 * If we can't power off, we force the CPU to crash and reset.
 */
void emergency_reboot() {
    struct {
        uint16_t limit;
        uint64_t base; // 32-bit kernels use uint32_t here
    } __attribute__((packed)) idtr = {0, 0};

    // Load a NULL IDT and trigger an interrupt
    asm volatile("lidt %0; int3" : : "m"(idtr));
}

/**
 * THE UNIVERSAL SHUTDOWN FUNCTION
 */
void k_shutdown(uint32_t acpi_ptr, uint16_t s5_val) {
    // 1. Try ACPI first (Real hardware)
    shutdown_acpi(acpi_ptr, s5_val);

    // 2. Try Emulator ports (VMs)
    shutdown_emulator();
    printk(T,"shutting down the kernel\n");
    // 3. If still alive, try to at least reboot
    emergency_reboot();
    printk(T,"rebooting the kernel.shutdown didn't work\n");
    // 4. Ultimate Halt
    while(1) { asm volatile("cli; hlt"); }
}