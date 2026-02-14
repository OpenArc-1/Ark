#include "ark/printk.h"
#include <stdint.h>
#include "../io/built-in.h"
#include "hw/vendor.h"
#include "ark/clear.h"
static int check_cpuid_present() {
    u32 supported;
    __asm__ __volatile__ (
        "pushfl\n"
        "pop %%eax\n"
        "mov %%eax, %%ecx\n"
        "xor $0x200000, %%eax\n"
        "push %%eax\n"
        "popfl\n"
        "pushfl\n"
        "pop %%eax\n"
        "xor %%ecx, %%eax\n"
        "mov %%eax, %0\n"
        "push %%ecx\n"
        "popfl\n"
        : "=r"(supported)
        :
        : "eax", "ecx", "cc"
    );
    return supported != 0;
}

static int is_x86_64() {
    u32 eax, edx;

    __asm__ __volatile__ (
        "cpuid"
        : "=a"(eax)
        : "a"(0x80000000)
        : "ebx", "ecx", "edx"
    );

    if (eax < 0x80000001) return 0;
    __asm__ __volatile__ (
        "pushl %%ebx\n"
        "cpuid\n"
        "popl %%ebx\n"
        : "=d"(edx), "=a"(eax) 
        : "a"(0x80000001)
        : "ecx"
    );

    return (edx & (1 << 29));
}

void cpu_verify() {
    clear_screen();
    printk("\n");
    printk("Scanning for a compataible hardware\n");
    printk(":: scanning for i386\n");

    if (!check_cpuid_present()) {
        printk("FATAL: CPUID not supported.\n");
        goto halt;
    }

    if (is_x86_64()) {
        printk("FATAL: x86_64 detected. i386 required.Use a compatible hardware.\n");
        goto halt;
    }

    printk(":: i386 confirmed. Continuing...\n");
    return;

halt:
    while (1) __asm__ ("cli; hlt");
}
