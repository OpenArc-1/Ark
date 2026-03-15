/**
 * arch/x86_64/cpu.c — Intel/x86_64 CPU identification for Ark
 *
 * Covers all Intel generations from Nehalem (1st gen, ~2008) through
 * Raptor Lake (13th gen, 2022) including every Xeon E3/E5/E7/EP/EX
 * Scalable Platinum/Gold/Silver/Bronze line.
 *
 * Also handles AMD, unknown vendors, and old CPUs gracefully.
 *
 * Detects and reports:
 *   - Vendor + brand string
 *   - Family/Model/Stepping → microarchitecture name + marketing generation
 *   - Xeon vs Core vs Atom product line from brand string
 *   - Physical cores / logical threads (CPUID leaf 0xB)
 *   - L1/L2/L3 cache sizes (CPUID leaf 4)
 *   - Feature flags: SSE4.2, AES-NI, AVX, AVX2, AVX-512, TSX, RDRAND…
 *   - Hyper-Threading, VT-x, NX
 *   - Base clock ratio from MSR_PLATFORM_INFO (Sandy Bridge+)
 *   - TjMax + current die temp from IA32_THERM_STATUS (Nehalem+)
 */

#include "ark/types.h"
#include "ark/arch.h"
#include "ark/printk.h"
#include "hw/vendor.h"

/* ── helpers ─────────────────────────────────────────────────────────────── */

static inline u64 rdmsr64(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

/* Trim leading spaces — brand strings often start with blanks */
static const char *brand_trim(const char *s) {
    while (*s == ' ') s++;
    return s;
}

/* Simple substring check without libc */
static int str_has(const char *hay, const char *needle) {
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

/* ── microarchitecture table ─────────────────────────────────────────────── */

typedef struct {
    u8          model;          /* extended model (upper nibble<<4 | lower nibble) */
    u8          gen;            /* Intel marketing generation (0 = pre-numbered)   */
    const char *uarch;
} intel_model_t;

/* All family-6 Intel CPUs (covers everything Pentium 4 era and later) */
static const intel_model_t f6_models[] = {
    /* Pre-numbered generations */
    { 0x0F, 0, "Merom (Core 2)"           },
    { 0x16, 0, "Merom-L (Core 2)"         },
    { 0x17, 0, "Penryn (Core 2)"          },
    { 0x1D, 0, "Dunnington (Xeon MP)"     },

    /* 1st gen — Nehalem / Westmere */
    { 0x1A, 1, "Nehalem"                  },
    { 0x1E, 1, "Nehalem"                  },
    { 0x1F, 1, "Nehalem"                  },
    { 0x2E, 1, "Nehalem-EX (Xeon)"        },
    { 0x25, 1, "Westmere"                 },
    { 0x2C, 1, "Westmere-EP (Xeon)"       },
    { 0x2F, 1, "Westmere-EX (Xeon)"       },

    /* 2nd gen — Sandy Bridge */
    { 0x2A, 2, "Sandy Bridge"             },
    { 0x2D, 2, "Sandy Bridge-E/EP (Xeon)" },

    /* 3rd gen — Ivy Bridge */
    { 0x3A, 3, "Ivy Bridge"               },
    { 0x3E, 3, "Ivy Bridge-EP/EX (Xeon)"  },

    /* 4th gen — Haswell */
    { 0x3C, 4, "Haswell"                  },
    { 0x3F, 4, "Haswell-E/EP (Xeon)"      },
    { 0x45, 4, "Haswell-ULT"              },
    { 0x46, 4, "Haswell-GT3e"             },

    /* 5th gen — Broadwell */
    { 0x3D, 5, "Broadwell"                },
    { 0x47, 5, "Broadwell-H"              },
    { 0x4F, 5, "Broadwell-E/EP (Xeon)"    },
    { 0x56, 5, "Broadwell-DE (Xeon D)"    },

    /* 6th gen — Skylake */
    { 0x4E, 6, "Skylake-U/Y"              },
    { 0x5E, 6, "Skylake-H/S"              },
    { 0x55, 6, "Skylake-SP/X (Xeon)"      },

    /* 7th gen — Kaby Lake */
    { 0x8E, 7, "Kaby Lake-U/Y"            },
    { 0x9E, 7, "Kaby Lake-H/S"            },

    /* 8th gen — Coffee/Whiskey Lake */
    { 0xEA, 8, "Coffee Lake"              },
    { 0xEB, 8, "Coffee Lake Refresh"      },

    /* 9th gen */
    { 0x9A, 9, "Coffee Lake-S (9th)"      },

    /* 10th gen — Comet/Ice Lake */
    { 0xA5, 10, "Comet Lake-H/S"          },
    { 0xA6, 10, "Comet Lake-U"            },
    { 0x7D, 10, "Ice Lake"                },
    { 0x7E, 10, "Ice Lake-U"              },

    /* 11th gen — Tiger Lake */
    { 0x8C, 11, "Tiger Lake-U"            },
    { 0x8D, 11, "Tiger Lake-H"            },

    /* 12th gen — Alder Lake */
    { 0x97, 12, "Alder Lake-S"            },
    { 0x9A, 12, "Alder Lake-P/H"          },

    /* 13th gen — Raptor Lake */
    { 0xB7, 13, "Raptor Lake-S"           },
    { 0xBA, 13, "Raptor Lake-P/H"         },

    /* Atom/low-power */
    { 0x1C,  0, "Bonnell (Atom)"          },
    { 0x26,  0, "Lincroft (Atom)"         },
    { 0x36,  0, "Saltwell (Atom)"         },
    { 0x37,  0, "Silvermont (Atom)"       },
    { 0x4D,  0, "Silvermont (Avoton)"     },
    { 0x5C,  0, "Goldmont (Apollo Lake)"  },
    { 0x5F,  0, "Goldmont (Denverton)"    },
    { 0x7A,  0, "Goldmont Plus"           },

    /* Xeon Phi */
    { 0x57,  0, "Knights Landing (Phi)"   },
    { 0x85,  0, "Knights Mill (Phi)"      },

    { 0, 0, NULL }
};

/* ── cache detection via CPUID leaf 4 ───────────────────────────────────── */

typedef struct { u32 l1d_kb, l1i_kb, l2_kb, l3_mb_x10; } cache_t;

static cache_t detect_caches(u32 max_leaf) {
    cache_t c = {0,0,0,0};
    if (max_leaf < 4) return c;
    for (u32 idx = 0; idx < 32; idx++) {
        u32 a,b,cc,d;
        ark_cpuid(4, idx, &a, &b, &cc, &d);
        u8 type  = a & 0x1F;
        if (!type) break;
        u8 level = (a >> 5) & 0x7;
        u32 ways  = ((b >> 22) & 0x3FF) + 1;
        u32 parts = ((b >> 12) & 0x3FF) + 1;
        u32 line  = (b & 0xFFF) + 1;
        u32 sets  = cc + 1;
        u32 kb    = (ways * parts * line * sets) / 1024;
        if (level == 1 && type == 1) c.l1d_kb = kb;
        if (level == 1 && type == 2) c.l1i_kb = kb;
        if (level == 2)              c.l2_kb  = kb;
        if (level == 3)              c.l3_mb_x10 = (kb * 10) / 1024; /* ×10 for 0.x */
    }
    return c;
}

/* ── core/thread topology via CPUID leaf 0xB ────────────────────────────── */

typedef struct { u32 phys, logical, smt; } topo_t;

static topo_t detect_topo(u32 max_leaf) {
    topo_t t = {1,1,1};
    if (max_leaf >= 0xB) {
        u32 a,b,c,d;
        ark_cpuid(0xB, 0, &a, &b, &c, &d);
        u32 type0 = (c >> 8) & 0xFF;
        u32 smt   = (type0 == 1) ? (b & 0xFFFF) : 1;
        if (!smt) smt = 1;
        ark_cpuid(0xB, 1, &a, &b, &c, &d);
        u32 type1 = (c >> 8) & 0xFF;
        u32 total = (type1 >= 2) ? (b & 0xFFFF) : smt;
        if (!total) total = smt;
        t.smt     = smt;
        t.logical = total;
        t.phys    = total / smt;
        if (!t.phys) t.phys = 1;
    } else {
        /* Leaf 1 fallback */
        u32 a,b,c,d;
        ark_cpuid(1, 0, &a, &b, &c, &d);
        u32 lpp = (b >> 16) & 0xFF;
        if (!lpp) lpp = 1;
        t.logical = lpp;
        t.phys    = lpp;
    }
    return t;
}

/* ── main cpu_verify ─────────────────────────────────────────────────────── */

void cpu_verify(void) {
    /* ── 1. Max leaf + vendor string ────────────────────────────────── */
    u32 max_leaf, vb, vc, vd;
    ark_cpuid(0, 0, &max_leaf, &vb, &vc, &vd);
    char vendor[13];
    ((u32*)vendor)[0] = vb;
    ((u32*)vendor)[1] = vd;   /* note: EDX before ECX in "GenuineIntel" */
    ((u32*)vendor)[2] = vc;
    vendor[12] = '\0';

    bool is_intel = (vb == 0x756E6547u &&   /* Genu */
                     vd == 0x49656E69u &&   /* ineI */
                     vc == 0x6C65746Eu);    /* ntel */
    bool is_amd   = (vb == 0x68747541u &&   /* Auth */
                     vd == 0x69746E65u &&   /* enti */
                     vc == 0x444D4163u);    /* cAMD */

    /* ── 2. Family / Model / Stepping ───────────────────────────────── */
    u32 r1a, r1b, r1c, r1d;
    ark_cpuid(1, 0, &r1a, &r1b, &r1c, &r1d);

    u32 stepping   = r1a & 0xF;
    u32 model_lo   = (r1a >> 4)  & 0xF;
    u32 family     = (r1a >> 8)  & 0xF;
    u32 ext_model  = (r1a >> 16) & 0xF;
    u32 ext_family = (r1a >> 20) & 0xFF;

    u32 eff_family = family;
    if (family == 0xF) eff_family += ext_family;

    u32 eff_model  = model_lo;
    if (family == 0x6 || family == 0xF)
        eff_model |= (ext_model << 4);

    /* ── 3. Brand string ─────────────────────────────────────────────── */
    char brand[49];
    brand[0] = '\0';
    u32 emax;
    ark_cpuid(0x80000000, 0, &emax, &vb, &vc, &vd);
    if (emax >= 0x80000004) {
        u32 *p = (u32*)brand;
        for (int i = 0; i < 3; i++) {
            u32 a,b,c,d;
            ark_cpuid(0x80000002 + (u32)i, 0, &a, &b, &c, &d);
            p[i*4+0]=a; p[i*4+1]=b; p[i*4+2]=c; p[i*4+3]=d;
        }
        brand[48] = '\0';
    }
    const char *bs = brand_trim(brand);

    /* ── 4. Microarchitecture lookup (Intel family 6) ────────────────── */
    const char *uarch = NULL;
    u8   gen   = 0;
    if (is_intel && eff_family == 6) {
        for (int i = 0; f6_models[i].uarch; i++) {
            if (f6_models[i].model == (u8)eff_model) {
                uarch = f6_models[i].uarch;
                gen   = f6_models[i].gen;
                break;
            }
        }
    }

    /* ── 5. Product line from brand string ───────────────────────────── */
    const char *line = "";
    if (is_intel) {
        if      (str_has(bs,"Xeon(R) Platinum")) line="Xeon Platinum";
        else if (str_has(bs,"Xeon(R) Gold"))     line="Xeon Gold";
        else if (str_has(bs,"Xeon(R) Silver"))   line="Xeon Silver";
        else if (str_has(bs,"Xeon(R) Bronze"))   line="Xeon Bronze";
        else if (str_has(bs,"Xeon(R) W"))        line="Xeon W";
        else if (str_has(bs,"Xeon(R) D"))        line="Xeon D";
        else if (str_has(bs,"Xeon(R) E7"))       line="Xeon E7";
        else if (str_has(bs,"Xeon(R) E5"))       line="Xeon E5";
        else if (str_has(bs,"Xeon(R) E3"))       line="Xeon E3";
        else if (str_has(bs,"Xeon(R) E-"))       line="Xeon E";
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

    /* ── 6. Feature flags ────────────────────────────────────────────── */
    /* CPUID.1 EDX */
    bool has_sse    = (r1d >> 25) & 1;
    bool has_sse2   = (r1d >> 26) & 1;
    bool has_htt    = (r1d >> 28) & 1;
    /* CPUID.1 ECX */
    bool has_sse3   = (r1c >>  0) & 1;
    bool has_ssse3  = (r1c >>  9) & 1;
    bool has_fma    = (r1c >> 12) & 1;
    bool has_sse41  = (r1c >> 19) & 1;
    bool has_sse42  = (r1c >> 20) & 1;
    bool has_aesni  = (r1c >> 25) & 1;
    bool has_xsave  = (r1c >> 26) & 1;
    bool has_avx    = (r1c >> 28) & 1;
    bool has_rdrand = (r1c >> 30) & 1;
    bool has_vmx    = (r1c >>  5) & 1;
    bool has_pcid   = (r1c >> 17) & 1;
    bool has_x2apic = (r1c >> 21) & 1;

    /* CPUID leaf 7 sub 0 */
    bool has_avx2    = false, has_avx512f  = false, has_avx512bw = false;
    bool has_avx512vl= false, has_tsx_hle  = false, has_tsx_rtm  = false;
    bool has_rdseed  = false, has_adx      = false, has_sha      = false;
    bool has_clwb    = false, has_mpx      = false;
    if (max_leaf >= 7) {
        u32 a,b,c,d;
        ark_cpuid(7, 0, &a, &b, &c, &d);
        has_tsx_hle   = (b >>  4) & 1;
        has_avx2      = (b >>  5) & 1;
        has_mpx       = (b >> 14) & 1;
        has_avx512f   = (b >> 16) & 1;
        has_tsx_rtm   = (b >> 11) & 1;
        has_avx512bw  = (b >> 30) & 1;
        has_avx512vl  = (b >> 31) & 1;
        has_rdseed    = (b >> 18) & 1;
        has_adx       = (b >> 19) & 1;
        has_sha       = (b >> 29) & 1;
        has_clwb      = (b >> 24) & 1;
    }

    /* Extended features (0x80000001) */
    bool has_nx = false, has_1gb = false, has_rdtscp = false;
    if (emax >= 0x80000001) {
        u32 a,b,c,d;
        ark_cpuid(0x80000001, 0, &a, &b, &c, &d);
        has_nx     = (d >> 20) & 1;
        has_1gb    = (d >> 26) & 1;
        has_rdtscp = (d >> 27) & 1;
    }

    /* ── 7. Topology ─────────────────────────────────────────────────── */
    topo_t topo  = detect_topo(max_leaf);

    /* ── 8. Cache ────────────────────────────────────────────────────── */
    cache_t cache = detect_caches(max_leaf);

    /* ── 9. Base clock via MSR_PLATFORM_INFO (0xCE) — Sandy Bridge+ ─── */
    u32 base_mhz = 0;
    if (is_intel && gen >= 2) {
        u64 msr = rdmsr64(0xCE);
        u32 ratio = (msr >> 8) & 0xFF;
        if (ratio) base_mhz = ratio * 100;
    }

    /* ── 9b. If no MSR base_mhz, use TSC calibration ──────────────── */
    if (!base_mhz) {
        extern u32 tsc_get_mhz(void);
        base_mhz = tsc_get_mhz();
    }

    /* ── 10. Thermal: TjMax + current temp — Nehalem+ ───────────────── */
    u32 tjmax = 0, temp_c = 0;
    if (is_intel && gen >= 1) {
        u64 tgt = rdmsr64(0x1A2);
        tjmax   = (tgt >> 16) & 0xFF;
        if (!tjmax) tjmax = 100;

        u64 therm = rdmsr64(0x19C);
        if (therm & (1u << 31)) {
            u32 delta = (therm >> 16) & 0x7F;
            temp_c = tjmax > delta ? tjmax - delta : 0;
        }
    }

    /* ── 11. Print ───────────────────────────────────────────────────── */
    printk("[CPU] %s\n", bs[0] ? bs : vendor);

    if (is_intel) {
        if (gen > 0 && uarch) {
            if (line[0])
                printk("[CPU] Intel %s | %s | Gen %u\n", line, uarch, (u32)gen);
            else
                printk("[CPU] Intel %s | Gen %u\n", uarch, (u32)gen);
        } else if (uarch) {
            printk("[CPU] Intel %s%s%s\n",
                   line[0] ? line : "", line[0] ? " | " : "", uarch);
        } else {
            printk("[CPU] Intel (family %u  model 0x%x  step %u)\n",
                   eff_family, eff_model, stepping);
        }
    } else if (is_amd) {
        printk("[CPU] AMD (family %u  model 0x%x  step %u)\n",
               eff_family, eff_model, stepping);
    } else {
        printk("[CPU] %s (family %u  model 0x%x)\n",
               vendor, eff_family, eff_model);
    }

    /* Cores / threads */
    if (has_htt && topo.smt > 1)
        printk("[CPU] %u cores  %u threads  (HyperThreading)\n",
               topo.phys, topo.logical);
    else
        printk("[CPU] %u core(s)\n", topo.phys);

    /* Base clock */
    if (base_mhz)
        printk("[CPU] Base clock: %u MHz\n", base_mhz);

    /* Cache */
    if (cache.l1d_kb || cache.l2_kb || cache.l3_mb_x10) {
        u32 l3_whole = cache.l3_mb_x10 / 10;
        u32 l3_frac  = cache.l3_mb_x10 % 10;
        if (l3_frac)
            printk("[CPU] Cache: L1d=%uK  L1i=%uK  L2=%uK  L3=%u.%uMB\n",
                   cache.l1d_kb, cache.l1i_kb, cache.l2_kb, l3_whole, l3_frac);
        else
            printk("[CPU] Cache: L1d=%uK  L1i=%uK  L2=%uK  L3=%uMB\n",
                   cache.l1d_kb, cache.l1i_kb, cache.l2_kb, l3_whole);
    }

    /* Features */
    printk("[CPU] Features:");
    if (has_sse)     printk(" SSE");
    if (has_sse2)    printk(" SSE2");
    if (has_sse3)    printk(" SSE3");
    if (has_ssse3)   printk(" SSSE3");
    if (has_sse41)   printk(" SSE4.1");
    if (has_sse42)   printk(" SSE4.2");
    if (has_fma)     printk(" FMA3");
    if (has_avx)     printk(" AVX");
    if (has_avx2)    printk(" AVX2");
    if (has_avx512f) {
        printk(" AVX-512");
        if (has_avx512bw)  printk("(BW)");
        if (has_avx512vl)  printk("(VL)");
    }
    if (has_aesni)   printk(" AES-NI");
    if (has_rdrand)  printk(" RDRAND");
    if (has_rdseed)  printk(" RDSEED");
    if (has_sha)     printk(" SHA");
    if (has_nx)      printk(" NX");
    if (has_vmx)     printk(" VT-x");
    if (has_tsx_rtm) printk(" TSX");
    if (has_pcid)    printk(" PCID");
    if (has_x2apic)  printk(" x2APIC");
    if (has_1gb)     printk(" 1G-pages");
    if (has_rdtscp)  printk(" RDTSCP");
    if (has_adx)     printk(" ADX");
    if (has_xsave)   printk(" XSAVE");
    if (has_clwb)    printk(" CLWB");
    if (has_mpx)     printk(" MPX");
    printk("\n");

    /* Thermal */
    if (temp_c && tjmax)
        printk("[CPU] Temp: %u C  (TjMax %u C)\n", temp_c, tjmax);
    else if (tjmax)
        printk("[CPU] TjMax: %u C\n", tjmax);

    if (has_vmx)
        printk("[CPU] VT-x available\n");
}

void cpu_name(void) {
    u32 emax, b, c, d;
    ark_cpuid(0x80000000, 0, &emax, &b, &c, &d);
    if (emax < 0x80000004) { printk("CPU: (no brand)\n"); return; }
    char brand[49];
    u32 *p = (u32*)brand;
    for (int i = 0; i < 3; i++) {
        u32 a,bb,cc,dd;
        ark_cpuid(0x80000002+(u32)i, 0, &a,&bb,&cc,&dd);
        p[i*4+0]=a; p[i*4+1]=bb; p[i*4+2]=cc; p[i*4+3]=dd;
    }
    brand[48] = '\0';
    printk("CPU: %s\n", brand_trim(brand));
}

void mem_verify(void) {
    printk("RAM: 64-bit long mode\n");
}

/*
 * exception64_handler — called from isr64_common (arch/x86_64/idt.S)
 */
typedef struct {
    u64 r15,r14,r13,r12,r11,r10,r9,r8;
    u64 rbp,rdi,rsi,rdx,rcx,rbx,rax;
    u64 vector, error_code;
    u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed)) cpu_frame64_t;

static const char *exc_names[] = {
    "#DE Divide Error",    "#DB Debug",
    "NMI",                 "#BP Breakpoint",
    "#OF Overflow",        "#BR Bound Range",
    "#UD Invalid Opcode",  "#NM No FPU",
    "#DF Double Fault",    "Coprocessor Overrun",
    "#TS Invalid TSS",     "#NP Seg Not Present",
    "#SS Stack Fault",     "#GP General Protection",
    "#PF Page Fault",      "Reserved",
    "#MF x87 FP",          "#AC Alignment Check",
    "#MC Machine Check",   "#XM SIMD FP",
    "#VE Virtualisation",  "Reserved","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved",
    "Reserved","Reserved","#SX Security","Reserved"
};

void exception64_handler(cpu_frame64_t *f) {
    u64 vec = f->vector;

    /* #DB spurious — clear DR6/DR7/TF */
    if (vec == 1) {
        __asm__ volatile(
            "xorq %%rax,%%rax\n"
            "mov %%rax,%%dr6\n"
            "mov %%rax,%%dr7\n" ::: "rax");
        f->rflags &= ~(u64)(1u<<8);
        return;
    }

    /* PIC IRQ 32-47 — just EOI and return */
    if (vec >= 32 && vec < 48) {
        __asm__ volatile("outb %0,%1"::"a"((u8)0x20),"Nd"((u16)0x20));
        if (vec >= 40)
            __asm__ volatile("outb %0,%1"::"a"((u8)0x20),"Nd"((u16)0xA0));
        return;
    }

    /* int 0x80 compat syscall */
    if (vec == 0x80) {
        extern u32 syscall_dispatch(u32,u32,u32,u32);
        f->rax = syscall_dispatch((u32)f->rax,(u32)f->rbx,
                                  (u32)f->rcx,(u32)f->rdx);
        return;
    }

    /* Fatal exception */
    const char *name = (vec < 32) ? exc_names[vec] : "Unknown";
    printk("\n*** EXCEPTION %llu: %s ***\n", vec, name);
    printk("Error=0x%llx  RIP=0x%llx  RSP=0x%llx\n",
           f->error_code, f->rip, f->rsp);
    printk("RAX=0x%llx  RBX=0x%llx  RCX=0x%llx  RDX=0x%llx\n",
           f->rax, f->rbx, f->rcx, f->rdx);
    if (vec == 14) {
        u64 cr2; __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
        printk("CR2=0x%llx\n", cr2);
    }
    printk("System halted.\n");
    __asm__ volatile("cli");
    for(;;) __asm__ volatile("hlt");
}
