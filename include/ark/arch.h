/**
 * include/ark/arch.h — Architecture abstraction for Ark kernel
 * Works for both x86 (32-bit) and x86_64 (64-bit) builds.
 */
#pragma once
#include "ark/types.h"
#include "ark/printk.h"

/* ── Bit width ─────────────────────────────────────────────────── */
#ifndef ARK_BITS
#  if defined(CONFIG_64BIT) && CONFIG_64BIT
#    define ARK_BITS 64
#  else
#    define ARK_BITS 32
#  endif
#endif

/* ── Pointer-sized types ───────────────────────────────────────── */
#if ARK_BITS == 64
  typedef u64  uptr;
  typedef u64  phys_addr_t;
  typedef i64  iptr;
  #define UPTR_MAX  0xFFFFFFFFFFFFFFFFULL
  #define ARCH_STR  "x86_64"
#else
  typedef u32  uptr;
  typedef u32  phys_addr_t;
  typedef i32  iptr;
  #define UPTR_MAX  0xFFFFFFFFUL
  #define ARCH_STR  "x86"
#endif

#define PAGE_SIZE        4096u
#define PAGE_SHIFT       12
#define PAGE_ALIGN(x)    (((uptr)(x) + PAGE_SIZE-1) & ~(uptr)(PAGE_SIZE-1))
#define PAGE_ALIGN_DOWN(x) ((uptr)(x) & ~(uptr)(PAGE_SIZE-1))

/* ── I/O ports ─────────────────────────────────────────────────── */
static inline void io_outb(u16 p, u8  v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void io_outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void io_outl(u16 p, u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline u8   io_inb(u16 p){ u8  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u16  io_inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u32  io_inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

/* ── CPUID ─────────────────────────────────────────────────────────
 * Single unified signature used everywhere in the kernel:
 *   ark_cpuid(leaf, subleaf, &eax, &ebx, &ecx, &edx)
 * Pass subleaf=0 when not needed (e.g. leaf 0, 0x80000000, etc.)
 * This matches the init_api.h function pointer signature exactly.
 */
static inline void ark_cpuid(u32 leaf, u32 subleaf,
                              u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    u32 a=0,b=0,c=0,d=0;
    __asm__ __volatile__("cpuid"
        : "=a"(a),"=b"(b),"=c"(c),"=d"(d)
        : "a"(leaf),"c"(subleaf));
    if(eax)*eax=a; if(ebx)*ebx=b; if(ecx)*ecx=c; if(edx)*edx=d;
}

/* Convenience: leaf only (subleaf=0) */
static inline void ark_cpuid_l(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    ark_cpuid(leaf, 0, eax, ebx, ecx, edx);
}

/* ── CPU control ───────────────────────────────────────────────── */
static inline void cpu_halt(void)       { __asm__ volatile("hlt"); }
static inline void cpu_cli(void)        { __asm__ volatile("cli"); }
static inline void cpu_sti(void)        { __asm__ volatile("sti"); }
static inline void cpu_pause(void)      { __asm__ volatile("pause"); }
static inline void cpu_hlt_forever(void){ for(;;){cpu_cli();cpu_halt();} }


/* ── x87 FPU / SSE / AVX initialisation ───────────────────────────
 *
 * Supports both x86 (32-bit) and x86_64 (64-bit) builds.
 *
 * x86_64 mandatory baseline (guaranteed on every x86_64 CPU):
 *   x87 FPU, MMX, SSE, SSE2
 *
 * Optionally detected and enabled via CPUID:
 *   SSE3        — CPUID.1:ECX[0]
 *   SSSE3       — CPUID.1:ECX[9]
 *   SSE4.1      — CPUID.1:ECX[19]
 *   SSE4.2      — CPUID.1:ECX[20]
 *   XSAVE       — CPUID.1:ECX[26]  required for AVX/AVX2/AVX-512
 *   AVX         — CPUID.1:ECX[28]  + XSAVE
 *   AVX2        — CPUID.7.0:EBX[5] + AVX
 *   AVX-512F    — CPUID.7.0:EBX[16]+ AVX  (detected, not yet enabled)
 *
 * CR0 bits (both x86 and x86_64, but 64-bit register on x86_64):
 *   MP  (bit 1) set     — WAIT/FWAIT check TS flag
 *   EM  (bit 2) cleared — native FP execution, no emulation
 *   TS  (bit 3) cleared — no lazy FPU trap at boot time
 *   ET  (bit 4) set     — x87 extension type (486+, always 1)
 *   NE  (bit 5) set     — native x87 error reporting via #MF
 *
 * CR4 bits:
 *   OSFXSR     (bit  9) — OS supports FXSAVE/FXRSTOR (needed for SSE)
 *   OSXMMEXCPT (bit 10) — OS handles #XM SIMD FP exception
 *   OSXSAVE    (bit 18) — OS manages XSAVE extended state
 *                         Set whenever CPUID reports XSAVE, regardless
 *                         of whether AVX is present — this is correct
 *                         because OSXSAVE gates XSAVE itself, not AVX.
 *
 * XCR0 (written via XSETBV, only when XSAVE is present):
 *   bit 0 — x87/FPU  (must always be 1, hardware-required)
 *   bit 1 — SSE/XMM  (must be 1 whenever bit 2 is 1)
 *   bit 2 — AVX/YMM  (set only when AVX is detected)
 *
 *   The XCR0 value is AND-masked against CPUID.(EAX=0Dh,ECX=0):EAX
 *   (the CPU's own valid-bits mask) before XSETBV to guarantee we
 *   never #GP by enabling a state component the CPU doesn't support.
 *
 * IMPORTANT — register width on x86_64:
 *   MOV to/from CR0/CR4 requires a 64-bit GPR on x86_64.
 *   Using a 32-bit operand size causes a #UD. All CR reads/writes
 *   in the 64-bit path use u64 variables.
 */

/* XCR0 component bits */
#define XCR0_X87  (1ULL << 0)   /* x87 FPU state   — always required */
#define XCR0_SSE  (1ULL << 1)   /* SSE/XMM state   — required if AVX */
#define XCR0_AVX  (1ULL << 2)   /* AVX/YMM hi-128  — set if AVX      */

static inline void ark_fpu_init(void)
{
    printk(T, "[FPU] init:");

#if ARK_BITS == 64
    /* ═══ x86_64 path ════════════════════════════════════════════
     *
     * Step 1 — CR0: enable x87 + set error-reporting mode.
     * Must use a 64-bit variable; MOV CR0,r32 is #UD in 64-bit mode.
     */
    u64 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);   /* EM=0  no FP emulation                */
    cr0 &= ~(1ULL << 3);   /* TS=0  no task-switch FPU trap        */
    cr0 |=  (1ULL << 1);   /* MP=1  WAIT checks TS                 */
    cr0 |=  (1ULL << 4);   /* ET=1  x87 extension type             */
    cr0 |=  (1ULL << 5);   /* NE=1  native #MF error reporting     */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    /* Step 2 — CPUID feature detection before touching CR4.
     * SSE2 is mandatory on x86_64 so we don't need to gate it.    */
    u32 c1_eax, c1_ebx, c1_ecx, c1_edx;
    ark_cpuid(1, 0, &c1_eax, &c1_ebx, &c1_ecx, &c1_edx);

    u32 has_sse3   = (c1_ecx >> 0)  & 1u;
    u32 has_ssse3  = (c1_ecx >> 9)  & 1u;
    u32 has_sse41  = (c1_ecx >> 19) & 1u;
    u32 has_sse42  = (c1_ecx >> 20) & 1u;
    u32 has_xsave  = (c1_ecx >> 26) & 1u;  /* XSAVE/XRSTOR + XCR0 */
    u32 has_avx    = (c1_ecx >> 28) & 1u;  /* 256-bit YMM regs     */

    /* CPUID leaf 7, subleaf 0 — structured extended features */
    u32 l7_eax, l7_ebx, l7_ecx, l7_edx;
    ark_cpuid(7, 0, &l7_eax, &l7_ebx, &l7_ecx, &l7_edx);
    u32 has_avx2    = has_avx & ((l7_ebx >> 5)  & 1u); /* AVX2      */
    u32 has_avx512f = has_avx & ((l7_ebx >> 16) & 1u); /* AVX-512F  */

    /* Step 3 — CR4: SSE support flags + OSXSAVE.
     * OSFXSR and OSXMMEXCPT are always set (SSE2 is mandatory).
     * OSXSAVE is set whenever the CPU has XSAVE — it enables the
     * XSAVE instruction itself and is required before XSETBV.
     * It is NOT conditional on AVX being present.                  */
    u64 cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL <<  9);   /* OSFXSR     — FXSAVE/FXRSTOR          */
    cr4 |= (1ULL << 10);   /* OSXMMEXCPT — OS handles #XM           */
    if (has_xsave)
        cr4 |= (1ULL << 18); /* OSXSAVE  — enables XSAVE + XSETBV  */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    /* Step 4 — XCR0: tell CPU which extended state the OS manages.
     * Only reachable when XSAVE is present (OSXSAVE just set above).
     *
     * We build our desired XCR0 value then AND it with the CPU's
     * supported-features mask from CPUID.(EAX=0Dh,ECX=0):EAX.
     * This prevents a #GP if we ask for a bit the CPU doesn't know. */
    if (has_xsave) {
        u64 xcr0_want = XCR0_X87 | XCR0_SSE;   /* always required  */
        if (has_avx)
            xcr0_want |= XCR0_AVX;

        /* Read CPU's valid XCR0 bitmask from leaf 0Dh, subleaf 0  */
        u32 xd_eax, xd_ebx, xd_ecx, xd_edx;
        ark_cpuid(0x0D, 0, &xd_eax, &xd_ebx, &xd_ecx, &xd_edx);
        u64 xcr0_valid = (u64)xd_eax | ((u64)xd_edx << 32);

        u64 xcr0 = xcr0_want & xcr0_valid;
        /* x87 bit 0 must always be 1 — restore it if mask wiped it */
        xcr0 |= XCR0_X87;

        __asm__ volatile(
            "xsetbv"
            :: "c"((u32)0),
               "a"((u32)(xcr0 & 0xFFFFFFFFu)),
               "d"((u32)(xcr0 >> 32))
        );
    }

    /* Log what we enabled */
    printk(" x87 SSE SSE2");           /* mandatory on x86_64      */
    if (has_sse3)    printk(" SSE3");
    if (has_ssse3)   printk(" SSSE3");
    if (has_sse41)   printk(" SSE4.1");
    if (has_sse42)   printk(" SSE4.2");
    if (has_xsave)   printk(" XSAVE");
    if (has_avx)     printk(" AVX");
    if (has_avx2)    printk(" AVX2");
    if (has_avx512f) printk(" AVX-512F");

#else
    /* ═══ x86 (32-bit) path ══════════════════════════════════════
     *
     * Step 1 — CPUID first so we know which CR4 bits are safe.    */
    u32 c1_eax, c1_ebx, c1_ecx, c1_edx;
    ark_cpuid(1, 0, &c1_eax, &c1_ebx, &c1_ecx, &c1_edx);
    u32 has_sse  = (c1_edx >> 25) & 1u;
    u32 has_sse2 = (c1_edx >> 26) & 1u;
    u32 has_sse3 = (c1_ecx >>  0) & 1u;

    /* Step 2 — CR0 */
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1u << 2);   /* EM=0 */
    cr0 &= ~(1u << 3);   /* TS=0 */
    cr0 |=  (1u << 1);   /* MP=1 */
    cr0 |=  (1u << 4);   /* ET=1 */
    cr0 |=  (1u << 5);   /* NE=1 */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    /* Step 3 — CR4: only set SSE bits if the CPU actually has SSE */
    if (has_sse) {
        u32 cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1u << 9);    /* OSFXSR     */
        cr4 |= (1u << 10);   /* OSXMMEXCPT */
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
    }

    printk(" x87");
    if (has_sse)  printk(" SSE");
    if (has_sse2) printk(" SSE2");
    if (has_sse3) printk(" SSE3");
#endif

    /* Final step — FNINIT: reset x87 to well-known state.
     * Control word → 0x037F: all exceptions masked, round-to-nearest,
     * 80-bit extended precision.  Safe to call on both paths.      */
    __asm__ volatile("fninit");

    printk("\n");
    printk(T, "[FPU] ready (fninit, CW=0x037F)\n");
}

/* ── Compiler hints ────────────────────────────────────────────── */
#define ARK_UNUSED(x)   ((void)(x))
#define ARK_NORETURN    __attribute__((noreturn))
#define ARK_PACKED      __attribute__((packed))
#define ARK_ALIGNED(n)  __attribute__((aligned(n)))
#define ARK_SECTION(s)  __attribute__((section(s)))
#define ARK_USED        __attribute__((used))
