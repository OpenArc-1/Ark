/**
 * include/hw/pager.h — Paging API (32-bit only; 64-bit uses boot.S setup)
 */
#pragma once
#include "ark/types.h"

#define PAGE_SIZE    4096
#define PAGE_PRESENT 0x01
#define PAGE_RW      0x02
#define PAGE_USER    0x04

#if !defined(CONFIG_64BIT) || !CONFIG_64BIT
void init_paging(void);
void map_page(u32 virt, u32 phys, u32 flags);
void map_region(u32 virt, u32 phys, u32 size, u32 flags);
u32  get_phys_addr(u32 virt);
#endif
