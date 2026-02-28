#pragma once
#include "ark/types.h"

u8  mmio_read8 (volatile void *base, u32 reg);
u16 mmio_read16(volatile void *base, u32 reg);
u32 mmio_read32(volatile void *base, u32 reg);

void mmio_write8 (volatile void *base, u32 reg, u8  val);
void mmio_write16(volatile void *base, u32 reg, u16 val);
void mmio_write32(volatile void *base, u32 reg, u32 val);
void* mmio_map(u32 phys, u32 size);
