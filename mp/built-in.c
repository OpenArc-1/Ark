#include "ark/types.h"
#include "../io/built-in.h"  
#include "ark/printk.h"

#define BIOS_DATA ((u8*)0x8000)

/* ---------------- Helpers ---------------- */

static u32 time = 0;

/* Simple number printing for kernel (no printf) */
static void print_num(u32 num) {
    char buf[12];
    int i = 0;

    if (num == 0) {
        printk("0");
        return;
    }

    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }

    while (i--) {
        char c[2] = { buf[i], 0 };
        printk(c);
    }
}

/* ---------------- CPU INFO ---------------- */

/* Get CPU vendor string */
void get_cpu_vendor(char* out) {
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

/* ---------------- SYSTEM INFO DISPLAY ---------------- */

#if !defined(CONFIG_64BIT) || !CONFIG_64BIT
void show_sysinfo_bios(void) {
    // Read BIOS memory block
    u16 conv_mem = *(u16*)(BIOS_DATA + 0);
    u16 ext_mem  = *(u16*)(BIOS_DATA + 2);
    u8 hour = BIOS_DATA[4];
    u8 min  = BIOS_DATA[5];
    u8 sec  = BIOS_DATA[6];
    u8 video_mode = BIOS_DATA[7];

    u32 total_mem_mb = (conv_mem + ext_mem) / 1024;

    char cpu_vendor[13];
    get_cpu_vendor(cpu_vendor);

    printk(T,"Boot environment initialized\n");
    printk(T,"vendor: ");
    printk(cpu_vendor);
    printk("\n");

    printk(T,"conventional: ");
    print_num(conv_mem);
    printk(" KB\n");

    printk(T,"extended: ");
    print_num(ext_mem);
    printk(" KB\n");

    printk(T,"total ram: ");
    print_num(total_mem_mb);
    printk(" MB\n");

    printk(T,"time: ");
    if (hour < 10) printk("0");
    print_num(hour);
    printk(":");
    if (min < 10) printk("0");
    print_num(min);
    printk(":");
    if (sec < 10) printk("0");
    print_num(sec);
    printk("\n");

    printk(T,"mode: ");
    print_num(video_mode);
    printk("\n");

    printk(T,"System ready.\n");
}

#else
/* 64-bit stub: BIOS data area not available in long mode */
void show_sysinfo_bios(void) {
    /* no-op in 64-bit builds */
}
#endif /* CONFIG_64BIT */

