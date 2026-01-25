// mmio.c
#include <stdint.h>

uint8_t  mmio_read8 (volatile void *base, uint32_t reg)  { return *(volatile uint8_t *) ((uint8_t*)base + reg); }
uint16_t mmio_read16(volatile void *base, uint32_t reg)  { return *(volatile uint16_t*)((uint8_t*)base + reg); }
uint32_t mmio_read32(volatile void *base, uint32_t reg)  { return *(volatile uint32_t*)((uint8_t*)base + reg); }

void mmio_write8 (volatile void *base, uint32_t reg, uint8_t  val) { *(volatile uint8_t *) ((uint8_t*)base + reg) = val; }
void mmio_write16(volatile void *base, uint32_t reg, uint16_t val) { *(volatile uint16_t*)((uint8_t*)base + reg) = val; }
void mmio_write32(volatile void *base, uint32_t reg, uint32_t val) { *(volatile uint32_t*)((uint8_t*)base + reg) = val; }
void* mmio_map(uint32_t phys, uint32_t size) {
    (void)size;
    return (void*)(uintptr_t)phys;
}
