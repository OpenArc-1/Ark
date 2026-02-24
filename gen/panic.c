/**
 * Kernel panic implementation.
 */

#include "ark/shutdown.h"
#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "./init.h"
#include "ark/pci.h" //new mod created by yahya mokhlis
#include "ark/input.h"
#include "hw/vendor.h"

static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

void kernel_panic(const char *msg) {
    __asm__ __volatile__("cli");  // Stop interrupts
    busy_delay(20000000);
    busy_delay(20000000);
    printk(T,"CPU for this run is : ");
    cpu_name();
    printk("\n");
    busy_delay(20000000);
    printk(T," please recompile with the </task>\n");
    printk(T," Fail to boot into any task to sync\n");

    printk(T," A fatal kernel error has occurred.\n");
    printk(T," The system has been halted to prevent corruption.\n");

    if (msg) {
        printk(T," Panic reason : %s\n", msg);
    } else {
        printk(T," Panic reason : Unknown fatal error\n");
    }

    printk(T,"System state  : HALTED\n");
    printk(T,"Kernel mode   : Protected\n");

    printk(T,"If this problem persists, check drivers, memory,\n");
    printk(T,"or recent kernel changes.\n");
    printk(T,"restart the system\n");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}


void mascot() {
    printk("        .~.~.~.\n");
    printk("      .'       '.\n");
    printk("     /   ^   ^   \\\n");
    printk("    |    (. .)    |\n");
    printk("    |     )-(     |\n");
    printk("    |    / V \\    |\n");
    printk("   /|   /     \\   |\\\n");
    printk("  / |  / ,-=-. \\  | \\\n");
    printk(" /  \\_/   | |   \\_/  \\\n");
    printk("/   / \\  _| |_  / \\   \\\n");
    printk("\\__/ /\\/       \\/\\ \\__/\n");
    printk("    /  ~~~   ~~~  \\\n");
    printk("   /_______________\\\n");
}
