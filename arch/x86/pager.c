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

/* 32-bit page directory + one page table (identity maps first 4 MiB) */
ARK_ALIGNED(4096) static u32 pg_dir[1024];
ARK_ALIGNED(4096) static u32 pg_tab_low[1024];   /* 0 – 4 MiB  */
ARK_ALIGNED(4096) static u32 pg_tab_kern[1024];  /* 4 – 8 MiB  */

void init_paging(void) {
    /* Clear directory */
    for (int i = 0; i < 1024; i++) pg_dir[i] = 0;

    /* Identity-map 0–4 MiB */
    for (u32 i = 0; i < 1024; i++)
        pg_tab_low[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    pg_dir[0] = ((u32)pg_tab_low & ~0xFFF) | PAGE_PRESENT | PAGE_RW;

    /* Identity-map 4–8 MiB (kernel) */
    for (u32 i = 0; i < 1024; i++)
        pg_tab_kern[i] = (0x400000 + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    pg_dir[1] = ((u32)pg_tab_kern & ~0xFFF) | PAGE_PRESENT | PAGE_RW;

    /* Recursive mapping at slot 1023 */
    pg_dir[1023] = ((u32)pg_dir & ~0xFFF) | PAGE_PRESENT | PAGE_RW;

    /* Load CR3 and enable paging */
    __asm__ volatile(
        "movl %0, %%cr3\n\t"
        "movl %%cr0, %%eax\n\t"
        "orl  $0x80000001, %%eax\n\t"
        "movl %%eax, %%cr0\n\t"
        :: "r"(pg_dir) : "eax");

    printk("Pager (32-bit): identity mapped 0-8 MiB\n");
}

void map_page(u32 virt, u32 phys, u32 flags) {
    u32 pd_idx = virt >> 22;
    u32 pt_idx = (virt >> 12) & 0x3FF;

    if (!(pg_dir[pd_idx] & PAGE_PRESENT)) {
        /* No page table for this PDE — kernel_panic or allocate */
        (void)pd_idx; (void)pt_idx; (void)phys; (void)flags;
        return;
    }

    u32 *pt = (u32 *)(pg_dir[pd_idx] & ~0xFFF);
    pt[pt_idx] = (phys & ~0xFFF) | flags | PAGE_PRESENT;
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
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
