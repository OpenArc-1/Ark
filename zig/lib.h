/**
 * zig/lib.h  —  C interface to zig/src/lib.zig
 *
 * Supports both x86 (32-bit) and x86_64 (64-bit) Ark builds.
 * phys_addr_t resolves to u32 or u64 via include/ark/arch.h.
 *
 * ── Enable ────────────────────────────────────────────────────────────────
 *   Set ZIG_ENABLE=1 in .kconfig and rebuild.
 *
 * ── Typical integration in arch/x86/pmm.c  ───────────────────────────────
 *
 *   #include "zig/lib.h"
 *
 *   void pmm_init_from_multiboot(multiboot_info_t *mbi) {
 *       // ... existing bitmap init unchanged ...
 *
 *       extern char _kernel_start[], _kernel_end[];
 *       ark_bscan_init(mbi,
 *                      (phys_addr_t)_kernel_start,
 *                      (phys_addr_t)_kernel_end);
 *
 *       printk(T, "bscan: %u MiB free in %u buddy blocks\n",
 *              (u32)(ark_bscan_free_bytes() >> 20),
 *              ark_bscan_free_blocks());
 *   }
 *
 * ── Allocating pages  ─────────────────────────────────────────────────────
 *
 *   // order 0 = PAGE_SIZE (4 KiB)
 *   phys_addr_t page = ark_bscan_alloc(0);
 *   if (!page) kernel_panic("PMM: out of memory");
 *
 *   // order 1 = 8 KiB, order 2 = 16 KiB, ..., order 10 = 4 MiB
 *   phys_addr_t bigpage = ark_bscan_alloc(2);
 *
 *   ark_bscan_free(page, 0);
 */
#pragma once
#include "ark/types.h"
#include "ark/arch.h"        /* phys_addr_t: u32 on x86, u64 on x86_64 */
#include "ark/multiboot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ark_bscan_init — Scan the Multiboot memory map and build buddy stacks.
 *
 * Skips the low 1 MiB (BIOS/IVT area) and the kernel image
 * [kernel_start, kernel_end) automatically.
 *
 * @mbi:          Multiboot info pointer from the bootloader.
 * @kernel_start: Physical start of kernel (_kernel_start linker symbol).
 * @kernel_end:   Physical end   of kernel (_kernel_end   linker symbol).
 */
void ark_bscan_init(const multiboot_info_t *mbi,
                    phys_addr_t kernel_start,
                    phys_addr_t kernel_end);

/**
 * ark_bscan_alloc — Allocate one block of (PAGE_SIZE << order) bytes.
 *
 * Falls back to splitting a higher-order block (buddy split) if the
 * requested order is empty.
 *
 * @order:  0=4KiB  1=8KiB  2=16KiB ... 10=4MiB  (11=8MiB on x86_64)
 * @return: Physical address, or 0 on OOM.
 */
phys_addr_t ark_bscan_alloc(u32 order);

/**
 * ark_bscan_free — Return a block to the buddy pool.
 *
 * Coalesces with the buddy if it is free, propagating upward until
 * no further coalescing is possible.
 *
 * @addr:  Physical address from ark_bscan_alloc().
 * @order: Must match the order used when the block was allocated.
 */
void ark_bscan_free(phys_addr_t addr, u32 order);

/** Total free bytes tracked across all orders. */
phys_addr_t ark_bscan_free_bytes(void);

/** Total free block count across all orders. */
u32 ark_bscan_free_blocks(void);

/** Free block count at a specific order. */
u32 ark_bscan_free_blocks_at_order(u32 order);

#ifdef __cplusplus
}
#endif
