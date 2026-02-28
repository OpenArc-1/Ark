/**
 * include/hw/pmm.h — Physical Memory Manager API (arch-independent)
 * 32-bit and 64-bit builds both implement this interface.
 */
#pragma once
#include "ark/types.h"
#include "ark/arch.h"

void        pmm_init(u32 mem_kb);
phys_addr_t pmm_alloc_frame(void);
void        pmm_free_frame(phys_addr_t addr);
u32         pmm_free_frames(void);
u32         pmm_total_frames(void);
