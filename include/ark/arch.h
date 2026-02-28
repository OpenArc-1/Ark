/**
 * include/ark/arch.h — Architecture abstraction for Ark kernel
 * Works for both x86 (32-bit) and x86_64 (64-bit) builds.
 */
#pragma once
#include "ark/types.h"

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

/* ── Compiler hints ────────────────────────────────────────────── */
#define ARK_UNUSED(x)   ((void)(x))
#define ARK_NORETURN    __attribute__((noreturn))
#define ARK_PACKED      __attribute__((packed))
#define ARK_ALIGNED(n)  __attribute__((aligned(n)))
#define ARK_SECTION(s)  __attribute__((section(s)))
#define ARK_USED        __attribute__((used))
