#include <stdint.h>

/* Intel x86 Local APIC Memory Map (Standard) */
#define LAPIC_BASE 0xFEE00000

/* Register Offsets */
#define LAPIC_TPR    0x080
#define LAPIC_EOI    0x0B0
#define LAPIC_SVR    0x0F0
#define LAPIC_TIMER  0x320
#define LAPIC_TICR   0x380
#define LAPIC_TDCR   0x3E0

static volatile uint32_t* lapic = (uint32_t*)LAPIC_BASE;

/**
 * Enables the APIC hardware.
 * Must be called after GDT/IDT are loaded.
 */
void intel_86_init_apic() {
    // 1. Enable APIC via IA32_APIC_BASE MSR (0x1B)
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x1B));
    low |= 0x800; // Set bit 11 (Global Enable)
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0x1B));

    // 2. Software Enable (Bit 8) and set Spurious Vector to 0xFF
    lapic[LAPIC_SVR / 4] = (1 << 8) | 0xFF;

    // 3. Set Task Priority to 0 (Accept all interrupts)
    lapic[LAPIC_TPR / 4] = 0;
}

/**
 * Configures the hardware timer for preemptive multitasking.
 * @param count: The divisor for the timer tick.
 */
void intel_86_start_timer(uint32_t count) {
    // Set Divide Configuration (Divider = 16)
    lapic[LAPIC_TDCR / 4] = 0x3;
    // Set Timer vector to 0x20 and Periodic mode (bit 17)
    lapic[LAPIC_TIMER / 4] = 0x20 | (1 << 17);
    // Set initial count
    lapic[LAPIC_TICR / 4] = count;
}

/**
 * Signal the end of a hardware interrupt.
 */
void intel_86_eoi() {
    lapic[LAPIC_EOI / 4] = 0;
}
