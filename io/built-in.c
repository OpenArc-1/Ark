#include "built-in.h"

u8  inb(u16 port){ u8  r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }
u16 inw(u16 port){ u16 r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(port)); return r; }
u32 inl(u16 port){ u32 r; __asm__ volatile("inl %1,%0":"=a"(r):"Nd"(port)); return r; }

void outb(u16 port, u8  v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
void outw(u16 port, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port)); }
void outl(u16 port, u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port)); }

void io_wait(void){ __asm__ volatile("outb %%al,$0x80"::"a"(0)); }
