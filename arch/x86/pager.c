/**
 * arch/x86/pager.c — 32-bit x86 paging (4 KiB pages, 2-level PT)
 *
 * Sets up identity mapping for the kernel.
 * NOT used in 64-bit builds (boot.S handles 64-bit paging directly).
 */
#include "ark/types.h"
#include "ark/arch.h"
#include "ark/printk.h"
#include "hw/pager.h"

/* 32-bit page directory + page tables for identity mapping 0-64 MiB.
 * 16 page tables × 1024 pages × 4 KiB = 64 MiB coverage.
 * This covers: low memory (0-1 MiB), kernel (4-8 MiB),
 * and userspace init (anywhere in 0-64 MiB). */
#define PG_TABLES 16   /* 16 × 4 MiB = 64 MiB */
ARK_ALIGNED(4096) static u32 pg_dir[1024];
ARK_ALIGNED(4096) static u32 pg_tabs[PG_TABLES][1024];

void init_paging(void) {
    /* Clear directory */
    for (int i = 0; i < 1024; i++) pg_dir[i] = 0;

    /* Identity-map 0–64 MiB across 16 page tables */
    for (int t = 0; t < PG_TABLES; t++) {
        for (u32 p = 0; p < 1024; p++)
            pg_tabs[t][p] = ((u32)t * 0x400000 + p * PAGE_SIZE)
                            | PAGE_PRESENT | PAGE_RW;
        pg_dir[t] = ((u32)pg_tabs[t] & ~0xFFFu) | PAGE_PRESENT | PAGE_RW;
    }

    /* Recursive mapping at slot 1023 for page table manipulation */
    pg_dir[1023] = ((u32)pg_dir & ~0xFFFu) | PAGE_PRESENT | PAGE_RW;

    /* Load CR3 and enable paging */
    __asm__ volatile(
        "movl %0, %%cr3\n\t"
        "movl %%cr0, %%eax\n\t"
        "orl  $0x80000001, %%eax\n\t"
        "movl %%eax, %%cr0\n\t"
        :: "r"(pg_dir) : "eax");

    printk("Pager (32-bit): identity mapped 0-64 MiB\n");
}

void map_page(u32 virt, u32 phys, u32 flags) {
    u32 pd_idx = virt >> 22;
    u32 pt_idx = (virt >> 12) & 0x3FF;

    /* If the PDE is within our statically allocated range, use it directly.
     * Otherwise silently skip (caller should only map within 0-64 MiB). */
    if (pd_idx < PG_TABLES) {
        pg_tabs[pd_idx][pt_idx] = (phys & ~0xFFFu) | flags | PAGE_PRESENT;
        __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
    }
}

void map_region(u32 virt, u32 phys, u32 size, u32 flags) {
    for (u32 off = 0; off < size; off += PAGE_SIZE)
        map_page(virt + off, phys + off, flags);
}

u32 get_phys_addr(u32 virt) {
    u32 pd_idx = virt >> 22;
    u32 pt_idx = (virt >> 12) & 0x3FF;
    if (!(pg_dir[pd_idx] & PAGE_PRESENT)) return 0;
    u32 *pt = (u32 *)(pg_dir[pd_idx] & ~0xFFF);
    return (pt[pt_idx] & ~0xFFF) | (virt & 0xFFF);
}
