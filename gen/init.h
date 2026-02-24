#define INIT_H
#ifndef INIT_H

static void busy_delay(u32 loops);

#include "ark/types.h"
#include "ark/printk.h"

/* ══════════════════════════════════════════════════════════════════════════
 * ══════════════════════════════════════════════════════════════════════════ */

extern u32 boot_us;  /* microseconds since kernel_main entered */

/* Call this instead of busy loops — keeps the boot clock ticking */
static inline void delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i)
        __asm__ __volatile__("");
    boot_us += (loops / 200);  /* ~20M loops = 100ms */
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

/* Timestamped printk — use like: printk("message %d\n", val) */
#define printk(fmt, ...) do { ts(); printk(fmt, ##__VA_ARGS__); } while(0)



#endif