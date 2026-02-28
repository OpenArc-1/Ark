/**
 * arch/x86/vendor.c — CPU identification for 32-bit x86 Ark builds
 */
#include "ark/types.h"
#include "ark/arch.h"
#include "ark/printk.h"
#include "hw/vendor.h"

static void get_brand(char buf[49]) {
    u32 eax, ebx, ecx, edx;
    ark_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004) {
        ark_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
        ((u32*)buf)[0]=ebx; ((u32*)buf)[1]=edx; ((u32*)buf)[2]=ecx;
        buf[12]='\0';
        return;
    }
    u32 *p = (u32 *)buf;
    for (int i = 0; i < 3; i++) {
        ark_cpuid(0x80000002 + (u32)i, 0, &eax, &ebx, &ecx, &edx);
        p[i*4+0]=eax; p[i*4+1]=ebx; p[i*4+2]=ecx; p[i*4+3]=edx;
    }
    buf[48] = '\0';
}

void cpu_verify(void) {
    char brand[49];
    get_brand(brand);
    printk("CPU (x86): %s\n", brand);
}

void cpu_name(void) {
    char brand[49];
    get_brand(brand);
    printk("CPU: %s\n", brand);
}
