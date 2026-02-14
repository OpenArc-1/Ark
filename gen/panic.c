/**
 * Kernel panic implementation.
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "init.h"
#include "ark/pci.h" //new mod created by yahya mokhlis
#include "ark/input.h"

static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

void kernel_panic(const char *msg) {
    __asm__ __volatile__("cli");  // Stop interrupts
    busy_delay(20000000);
    printk(":: Found something in ramfs.please renter the </TASK>\n");
    busy_delay(20000000);
    printk(":: type </TASK> to run it\n");
    busy_delay(20000000);
    //char input_buffer[12];
   // printk("path(/usr/): ");
    //input_read(input_buffer, sizeof(input_buffer), false);
    //printk("%s: NOT FOUND!!!\n", input_buffer);
    printk("[PS/2][K:found ps/2] please recompile with the </task>");
    printk(":: Fail to boot into any task to sync\n");

    printk(":: A fatal kernel error has occurred.\n");
    printk(":: The system has been halted to prevent corruption.\n\n");

    if (msg) {
        printk(":: Panic reason : %s\n", msg);
    } else {
        printk(":: Panic reason : Unknown fatal error\n");
    }

    printk("\n::System state  : HALTED\n");
    printk("::Kernel mode   : Protected\n");

    printk("::\n");
    printk(":: If this problem persists, check drivers, memory,\n");
    printk(":: or recent kernel changes.\n");
    printk(":: restart the system\n");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}



