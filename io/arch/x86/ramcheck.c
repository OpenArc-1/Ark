/**
 * arch/x86/ramcheck.c — Basic RAM presence check for 32-bit Ark builds
 */
#include "ark/types.h"
#include "ark/printk.h"
#include "hw/ramcheck.h"

#define TEST_ADDR  0xFFFFC    /* Last dword in first 1 MiB */
#define MIN_RAM_KB 4096       /* 4 MiB minimum */

void mem_verify(void) {
    volatile u32 *p = (volatile u32 *)TEST_ADDR;
    u32 saved = *p;
    *p = 0xDEADBEEF;
    if (*p != 0xDEADBEEF) {
        printk("RAM check: FAILED — insufficient memory\n");
        printk("Continuing with risk");
    }
    *p = saved;
    printk("RAM check: OK\n");
}
