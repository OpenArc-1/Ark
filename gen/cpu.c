/**
 * gen/cpu.c — Architecture-independent CPUID helpers.
 * ark_cpuid and ark_get_cpu_vendor are used by both the kernel
 * and exposed through the init_api table.
 */
#include "ark/types.h"
#include "ark/arch.h"

/* ark_cpuid is a static inline in arch.h — expose a real symbol for init_api */
void ark_cpuid_sym(u32 leaf, u32 subleaf,
                   u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    ark_cpuid(leaf, subleaf, eax, ebx, ecx, edx);
}

void ark_get_cpu_vendor(char out_13[13]) {
    u32 eax,ebx,ecx,edx;
    ark_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    ((u32*)out_13)[0]=ebx;
    ((u32*)out_13)[1]=edx;
    ((u32*)out_13)[2]=ecx;
    out_13[12]='\0';
}
