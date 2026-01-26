#pragma once
#include <stdint.h>

uint8_t  mmio_read8 (volatile void *base, uint32_t reg);
uint16_t mmio_read16(volatile void *base, uint32_t reg);
uint32_t mmio_read32(volatile void *base, uint32_t reg);

void mmio_write8 (volatile void *base, uint32_t reg, uint8_t  val);
void mmio_write16(volatile void *base, uint32_t reg, uint16_t val);
void mmio_write32(volatile void *base, uint32_t reg, uint32_t val);
void* mmio_map(uint32_t phys, uint32_t size);
