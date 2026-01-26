#include "ark/types.h"
#include "../io/built-in.h"  

#define BIOS_DATA ((u8*)0x8000)

/* ---------------- External Kernel Print ---------------- */

extern void printk(const char* str);

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

/* Log prefix like Linux dmesg [tag][time] */
static void log_prefix(const char* tag) {
    printk("[");
    printk(tag);
    printk("][    0.");
    print_num(time);
    printk("] ");
    time += 1234;  
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

    // Show logs
    log_prefix("bios");
    printk("Boot environment initialized\n");

    log_prefix("cpu");
    printk("vendor: ");
    printk(cpu_vendor);
    printk("\n");

    log_prefix("mem");
    printk("conventional: ");
    print_num(conv_mem);
    printk(" KB\n");

    log_prefix("mem");
    printk("extended: ");
    print_num(ext_mem);
    printk(" KB\n");

    log_prefix("mem");
    printk("total ram: ");
    print_num(total_mem_mb);
    printk(" MB\n");

    log_prefix("rtc");
    printk("time: ");
    if (hour < 10) printk("0");
    print_num(hour);
    printk(":");
    if (min < 10) printk("0");
    print_num(min);
    printk(":");
    if (sec < 10) printk("0");
    print_num(sec);
    printk("\n");

    log_prefix("video");
    printk("mode: ");
    print_num(video_mode);
    printk("\n");

    log_prefix("kernel");
    printk("System ready.\n");
}
