/*
 * gen/panic.c — Kernel panic + mascot
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/shutdown.h"
#include "ark/pci.h"
#include "ark/input.h"
#include "hw/vendor.h"
#include "./init.h"

#if CONFIG_DEBUG_PANIC_QR

void kernel_panic(const char *msg) {
    kernel_panic_with_qr(msg);
}

#else

void kernel_panic(const char *msg) {
    __asm__ __volatile__("cli");

    printk(T, "kernel panic - not syncing");

    if (msg)
        printk(T, "reason : %s\n", msg);
    else
        printk(T, "reason : unknown fatal error\n");

    printk(T, "CPU    : ");
    cpu_name();
    id_ldm();
    printk("\n");

    printk(T, "The system has been halted.\n");
    printk(T, "please restart");

    for (;;)
        __asm__ __volatile__("hlt");
}

#endif

void mascot(void) {
    printk("       .~.~.~.\n");
    printk("     .'       '.\n");
    printk("    /   ^   ^   \\\n");
    printk("   |    (. .)    |\n");
    printk("   |     )-(     |\n");
    printk("   |    / V \\    |\n");
    printk("  /|   /     \\   |\\\n");
    printk(" / |  / ,-=-. \\  | \\\n");
    printk("/  \\_/   | |   \\_/  \\\n");
    printk("   / \\  _| |_  / \\   \n");
    printk("  ___________________\n");
    printk("       Ark  :)\n");
}
