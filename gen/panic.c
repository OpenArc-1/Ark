/**
 * Kernel panic implementation.
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"

void kernel_panic(const char *msg) {
    printk("\n\n!!! KERNEL PANIC !!!\n");
    if (msg) {
        printk("%s\n", msg);
    }
    printk("System halted.\n");

    /* Halt the CPU forever. On a real system we might also try to
     * disable interrupts or trigger a reboot.
     */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

