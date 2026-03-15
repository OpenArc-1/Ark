// mmio.c
#include "ark/types.h"
#include "ark/arch.h"

u8  mmio_read8 (volatile void *base, u32 reg)  { return *(volatile u8 *) ((u8*)base + reg); }
u16 mmio_read16(volatile void *base, u32 reg)  { return *(volatile u16*)((u8*)base + reg); }
u32 mmio_read32(volatile void *base, u32 reg)  { return *(volatile u32*)((u8*)base + reg); }

void mmio_write8 (volatile void *base, u32 reg, u8  val) { *(volatile u8 *) ((u8*)base + reg) = val; }
void mmio_write16(volatile void *base, u32 reg, u16 val) { *(volatile u16*)((u8*)base + reg) = val; }
void mmio_write32(volatile void *base, u32 reg, u32 val) { *(volatile u32*)((u8*)base + reg) = val; }
void* mmio_map(u32 phys, u32 size) {
    (void)size;
    return (void*)(uptr)phys;
}
