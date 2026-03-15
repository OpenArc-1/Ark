/**
 * arch/x86/vendor.c — CPU identification for 32-bit x86 Ark builds
 * Mirrors arch/x86_64/cpu.c but compiles for -m32.
 */
#include "ark/types.h"
#include "ark/arch.h"
#include "ark/printk.h"
#include "hw/vendor.h"

static const char *brand_trim(const char *s) {
    while (*s == ' ') s++;
    return s;
}

static int str_has(const char *hay, const char *needle) {
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

typedef struct {
    u8 model; u8 gen; const char *uarch;
} intel_model_t;

static const intel_model_t f6_models[] = {
    { 0x0F, 0, "Merom (Core 2)"            },
    { 0x17, 0, "Penryn (Core 2)"           },
    { 0x1A, 1, "Nehalem"                   },
    { 0x1E, 1, "Nehalem"                   },
    { 0x2E, 1, "Nehalem-EX (Xeon)"         },
    { 0x25, 1, "Westmere"                  },
    { 0x2C, 1, "Westmere-EP (Xeon)"        },
    { 0x2F, 1, "Westmere-EX (Xeon)"        },
    { 0x2A, 2, "Sandy Bridge"              },
    { 0x2D, 2, "Sandy Bridge-E/EP (Xeon)"  },
    { 0x3A, 3, "Ivy Bridge"                },
    { 0x3E, 3, "Ivy Bridge-EP/EX (Xeon)"   },
    { 0x3C, 4, "Haswell"                   },
    { 0x3F, 4, "Haswell-E/EP (Xeon)"       },
    { 0x45, 4, "Haswell-ULT"               },
    { 0x3D, 5, "Broadwell"                 },
    { 0x4F, 5, "Broadwell-E/EP (Xeon)"     },
    { 0x56, 5, "Broadwell-DE (Xeon D)"     },
    { 0x4E, 6, "Skylake-U/Y"               },
    { 0x5E, 6, "Skylake-H/S"               },
    { 0x55, 6, "Skylake-SP/X (Xeon)"       },
    { 0x8E, 7, "Kaby Lake-U/Y"             },
    { 0x9E, 7, "Kaby Lake-H/S"             },
    { 0xA5,10, "Comet Lake"                },
    { 0x8C,11, "Tiger Lake"                },
    { 0x97,12, "Alder Lake-S"              },
    { 0xB7,13, "Raptor Lake-S"             },
    { 0, 0, NULL }
};

void cpu_verify(void) {
    /* Vendor */
    u32 max_leaf, vb, vc, vd;
    ark_cpuid(0, 0, &max_leaf, &vb, &vc, &vd);
    char vendor[13];
    ((u32*)vendor)[0]=vb; ((u32*)vendor)[1]=vd; ((u32*)vendor)[2]=vc;
    vendor[12]='\0';
    bool is_intel = (vb==0x756E6547u && vd==0x49656E69u && vc==0x6C65746Eu);

    /* Family/Model */
    u32 r1a,r1b,r1c,r1d;
    ark_cpuid(1, 0, &r1a, &r1b, &r1c, &r1d);
    u32 fam = (r1a>>8)&0xF, ext_fam=(r1a>>20)&0xFF;
    u32 mod = ((r1a>>4)&0xF)|((r1a>>12)&0xF0);
    if (fam==0xF) fam += ext_fam;

    /* Brand string */
    char brand[49]; brand[0]='\0';
    u32 emax,b,c,d;
    ark_cpuid(0x80000000,0,&emax,&b,&c,&d);
    if (emax >= 0x80000004) {
        u32 *p=(u32*)brand;
        for (int i=0;i<3;i++) {
            u32 a,bb,cc,dd;
            ark_cpuid(0x80000002+(u32)i,0,&a,&bb,&cc,&dd);
            p[i*4+0]=a;p[i*4+1]=bb;p[i*4+2]=cc;p[i*4+3]=dd;
        }
        brand[48]='\0';
    }
    const char *bs = brand_trim(brand);

    /* uarch */
    const char *uarch=NULL; u8 gen=0;
    if (is_intel && fam==6) {
        for (int i=0; f6_models[i].uarch; i++)
            if (f6_models[i].model==(u8)mod) { uarch=f6_models[i].uarch; gen=f6_models[i].gen; break; }
    }

    /* Product line */
    const char *line="";
    if (is_intel) {
        if      (str_has(bs,"Xeon(R) Platinum")) line="Xeon Platinum";
        else if (str_has(bs,"Xeon(R) Gold"))     line="Xeon Gold";
        else if (str_has(bs,"Xeon(R) Silver"))   line="Xeon Silver";
        else if (str_has(bs,"Xeon(R) Bronze"))   line="Xeon Bronze";
        else if (str_has(bs,"Xeon(R) E7"))       line="Xeon E7";
        else if (str_has(bs,"Xeon(R) E5"))       line="Xeon E5";
        else if (str_has(bs,"Xeon(R) E3"))       line="Xeon E3";
        else if (str_has(bs,"Xeon"))             line="Xeon";
        else if (str_has(bs,"Core(TM) i9"))      line="Core i9";
        else if (str_has(bs,"Core(TM) i7"))      line="Core i7";
        else if (str_has(bs,"Core(TM) i5"))      line="Core i5";
        else if (str_has(bs,"Core(TM) i3"))      line="Core i3";
        else if (str_has(bs,"Core(TM)2"))        line="Core 2";
        else if (str_has(bs,"Pentium"))          line="Pentium";
        else if (str_has(bs,"Celeron"))          line="Celeron";
        else if (str_has(bs,"Atom"))             line="Atom";
    }

    /* Features */
    bool has_sse  =(r1d>>25)&1, has_sse2=(r1d>>26)&1, has_htt=(r1d>>28)&1;
    bool has_sse3 =(r1c>> 0)&1, has_sse41=(r1c>>19)&1, has_sse42=(r1c>>20)&1;
    bool has_aesni=(r1c>>25)&1, has_avx=(r1c>>28)&1, has_vmx=(r1c>>5)&1;
    bool has_nx=false;
    if (emax>=0x80000001) { u32 a,bb,cc,dd; ark_cpuid(0x80000001,0,&a,&bb,&cc,&dd); has_nx=(dd>>20)&1; }

    /* Topology (leaf 1 only for 32-bit simplicity) */
    u32 lpp = (r1b>>16)&0xFF; if (!lpp) lpp=1;

    /* Print */
    printk("[CPU] %s\n", bs[0] ? bs : vendor);
    if (is_intel) {
        if (gen>0 && uarch) printk("[CPU] Intel %s%s%s | Gen %u\n",
            line[0]?line:"", line[0]?" | ":"", uarch, (u32)gen);
        else if (uarch)     printk("[CPU] Intel %s\n", uarch);
        else                printk("[CPU] Intel (fam %u  model 0x%x)\n", fam, mod);
    }
    if (has_htt && lpp>1)
        printk("[CPU] %u logical thread(s) (HT)\n", lpp);
    printk("[CPU] Features:");
    if (has_sse)   printk(" SSE");
    if (has_sse2)  printk(" SSE2");
    if (has_sse3)  printk(" SSE3");
    if (has_sse41) printk(" SSE4.1");
    if (has_sse42) printk(" SSE4.2");
    if (has_aesni) printk(" AES-NI");
    if (has_avx)   printk(" AVX");
    if (has_nx)    printk(" NX");
    if (has_vmx)   printk(" VT-x");
    printk("\n");
}

void cpu_name(void) {
    u32 emax,b,c,d;
    ark_cpuid(0x80000000,0,&emax,&b,&c,&d);
    if (emax < 0x80000004) { printk("CPU: (no brand)\n"); return; }
    char brand[49]; u32 *p=(u32*)brand;
    for (int i=0;i<3;i++) {
        u32 a,bb,cc,dd;
        ark_cpuid(0x80000002+(u32)i,0,&a,&bb,&cc,&dd);
        p[i*4+0]=a;p[i*4+1]=bb;p[i*4+2]=cc;p[i*4+3]=dd;
    }
    brand[48]='\0';
    printk("CPU: %s\n", brand_trim(brand));
}
