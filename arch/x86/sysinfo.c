#include "ark/types.h"
#include "ark/printk.h"

/* Real-mode BIOS Data Area (BDA) — physical 0x0400, always identity-mapped
 * in the first 1 MiB.  0x8000 was a QEMU-only accident; real hardware puts
 * conventional-memory size at 0x413 (2 bytes, KB), RTC clock at 0x46C–0x46E,
 * and video mode at 0x449. */
#define BDA          ((volatile u8 *)0x0400)
/* 0x400+0x13 = 0x413: conventional memory in KB (below 640 KiB) */
#define BDA_CONV_MEM  (*(volatile u16 *)(BDA + 0x13))
/* 0x400+0x49 = 0x449: current video mode */
#define BDA_VIDEO_MODE (*(volatile u8  *)(BDA + 0x49))



void get_cpu_vendor(char *out) {
    u32 ebx, ecx, edx;
    __asm__ volatile (
        "cpuid"
        : "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    );
    ((u32*)out)[0] = ebx;
    ((u32*)out)[1] = edx;
    ((u32*)out)[2] = ecx;
    out[12] = 0;
}



#if !defined(CONFIG_64BIT) || !CONFIG_64BIT
void show_sysinfo_bios(void) {
    /* Conventional memory below 640 KiB, from BDA offset 0x13 */
    u16 conv_mem   = BDA_CONV_MEM;
    u8  video_mode = BDA_VIDEO_MODE;

    char cpu_vendor[13];
    get_cpu_vendor(cpu_vendor);

    printk(T, "cpu: %s\n",              cpu_vendor);
    printk(T, "ram: conventional=%u KB\n", conv_mem);
    printk(T, "vga: mode=%u\n",         video_mode);
    printk(T, "bios: sysinfo ok\n");
}

#else
/* 64-bit: BIOS data area not accessible in long mode */
void show_sysinfo_bios(void) { }
#endif /* CONFIG_64BIT */

