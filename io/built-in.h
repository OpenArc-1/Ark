#pragma once
#include "ark/types.h"

#ifdef ARK_IO_INLINE
static inline u8   inb(u16 p){ u8  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u16  inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u32  inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outb(u16 p, u8  v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl(u16 p, u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline void io_wait(void){ __asm__ volatile("outb %%al,$0x80"::"a"(0)); }
#else
extern u8   inb(u16 port);
extern u16  inw(u16 port);
extern u32  inl(u16 port);
extern void outb(u16 port, u8  value);
extern void outw(u16 port, u16 value);
extern void outl(u16 port, u32 value);
extern void io_wait(void);
#endif

#if defined(CONFIG_BITS) && CONFIG_BITS == 64
static inline u64 inq(u16 port) {
    return (u64)inl(port) | ((u64)inl((u16)(port + 4)) << 32);
}
static inline void outq(u16 port, u64 v) {
    outl(port,            (u32)(v & 0xFFFFFFFFu));
    outl((u16)(port + 4), (u32)(v >> 32));
}
#endif
