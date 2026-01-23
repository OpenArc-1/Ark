/**
 * Kernel panic implementation.
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "init.h"

static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

void kernel_panic(const char *msg) {
    __asm__ __volatile__("cli");  // Stop interrupts
    busy_delay(20000000);
    printk("[    0.000210] type </TASK> to run it\n");
    busy_delay(20000000);
    printk("[    0.012000][K:found ps/2] please recompile with the </task>");
    printk("\n\n");
    printk("--------------------------------------------------\n");
    printk("                 K E R N E L   P A N I C           \n");
    printk("--------------------------------------------------\n\n");

    printk("A fatal kernel error has occurred.\n");
    printk("The system has been halted to prevent corruption.\n\n");

    if (msg) {
        printk("Panic reason : %s\n", msg);
    } else {
        printk("Panic reason : Unknown fatal error\n");
    }

    printk("\nSystem state  : HALTED\n");
    printk("Recovery      : Reboot required\n");
    printk("Kernel mode   : Protected\n");
    printk("\n");

    printk("--------------------------------------------------\n");
    printk("If this problem persists, check drivers, memory,\n");
    printk("or recent kernel changes.\n");
    printk("--------------------------------------------------\n");

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}


