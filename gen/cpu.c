/**
 * cpu.c - small CPU helpers for Ark
 */
 
 #include "ark/types.h"
 
 void ark_cpuid(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
     u32 a, b, c, d;
     __asm__ __volatile__(
         "cpuid"
         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
         : "a"(leaf), "c"(subleaf)
     );
 
     if (eax) *eax = a;
     if (ebx) *ebx = b;
     if (ecx) *ecx = c;
     if (edx) *edx = d;
 }
 
 void ark_get_cpu_vendor(char out_13[13]) {
     u32 ebx, ecx, edx;
     __asm__ __volatile__(
         "cpuid"
         : "=b"(ebx), "=c"(ecx), "=d"(edx)
         : "a"(0)
     );
 
     ((u32*)out_13)[0] = ebx;
     ((u32*)out_13)[1] = edx;
     ((u32*)out_13)[2] = ecx;
     out_13[12] = 0;
 }

