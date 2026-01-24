#include "mmio.h"

void* mmio_map(uint32_t phys_addr) {
    // arks physical address is already accessible.
    return (void*)phys_addr;
}
