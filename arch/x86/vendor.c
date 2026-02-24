#include "ark/printk.h"
#include <stdint.h>
#include <cpuid.h>
#include "../io/built-in.h"
#include "hw/vendor.h"
#include "ark/clear.h"

/*
 * check_cpuid_present - attempts to detect CPUID support by using
 * __get_cpuid_max which returns 0 if CPUID is unavailable on this CPU.
 * On very old pre-486 hardware this will return 0.
 */
static int check_cpuid_present() {
    return __get_cpuid_max(0, 0) != 0;
}

/*
 * is_x86_64 - checks for Long Mode support via extended CPUID leaf
 * 0x80000001, bit 29 of EDX (LM flag). If this bit is set the CPU
 * is x86_64 capable and therefore not a pure i386 target.
 */
static int is_x86_64() {
    u32 eax, ebx, ecx, edx;

    /* Check if extended leaves are available */
    if (__get_cpuid_max(0x80000000, 0) < 0x80000001)
        return 0;

    __cpuid(0x80000001, eax, ebx, ecx, edx);

    return (edx & (1 << 29)) != 0;
}

/*
 * get_cpu_name - retrieves the CPU brand string using CPUID leaves
 * 0x80000002 through 0x80000004 via GCC's __cpuid intrinsic.
 * Falls back to vendor ID string if brand string is unsupported.
 */
static void get_cpu_name(char *buf) {
    u32 eax, ebx, ecx, edx;
    u32 max_ext;
    int i;

    max_ext = __get_cpuid_max(0x80000000, 0);

    if (max_ext < 0x80000004) {
        /* Fall back to 12-byte vendor ID string */
        __cpuid(0, eax, ebx, ecx, edx);
        u32 *v = (u32 *)buf;
        v[0] = ebx;
        v[1] = edx;
        v[2] = ecx;
        buf[12] = '\0';
        return;
    }

    /* Collect 48-byte brand string from leaves 0x80000002-0x80000004 */
    u32 *p = (u32 *)buf;
    for (i = 0; i < 3; i++) {
        __cpuid(0x80000002 + i, eax, ebx, ecx, edx);
        p[i * 4 + 0] = eax;
        p[i * 4 + 1] = ebx;
        p[i * 4 + 2] = ecx;
        p[i * 4 + 3] = edx;
    }
    buf[48] = '\0';
}

void cpu_verify() {
    char cpu_name[49];

    clear_screen();
    printk("\n");
    printk(T, "Scanning for compatible hardware\n");
    printk(T, " scanning for i386\n");

    if (!check_cpuid_present()) {
        printk(T, "FATAL: CPUID not supported.\n");
        goto halt;
    }

    if (is_x86_64()) {
        printk(T, "FATAL: x86_64 detected. i386 required. Use compatible hardware.\n");
        goto halt;
    }

    get_cpu_name(cpu_name);
    printk(T, "i386 confirmed. Continuing...\n");
    printk(T, "CPU: ");
    printk(cpu_name);
    printk("\n");
    return;

halt:
    while (1);
}
void cpu_name(){
    char cpu_id[49];

    get_cpu_name(cpu_id);
    printk(cpu_id);
}