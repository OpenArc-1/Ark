#ifndef INIT_H
#define INIT_H

#include "ark/types.h"
#include "ark/printk.h"

/* Boot timestamp counter - microseconds since kernel_main entered */
extern u32 boot_us;

/* busy delay that also advances the boot clock */
static inline void delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i)
        __asm__ __volatile__("");
    boot_us += (loops / 200);
}

/* Print a Linux-style timestamp prefix: [    X.XXXXXX] */
static inline void ts(void) {
    u32 sec  = boot_us / 1000000u;
    u32 frac = boot_us % 1000000u;
    printk("[");
    if      (sec < 10)    printk("    ");
    else if (sec < 100)   printk("   ");
    else if (sec < 1000)  printk("  ");
    else if (sec < 10000) printk(" ");
    printk("%u.", sec);
    if      (frac < 10)     printk("00000");
    else if (frac < 100)    printk("0000");
    else if (frac < 1000)   printk("000");
    else if (frac < 10000)  printk("00");
    else if (frac < 100000) printk("0");
    printk("%u] ", frac);
}

#endif /* INIT_H */
