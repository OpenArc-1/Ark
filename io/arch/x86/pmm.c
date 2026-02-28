/**
 * arch/x86/pmm.c — 32-bit Physical Memory Manager for Ark kernel
 *
 * Bitmap allocator: each bit represents one 4 KiB page frame.
 * Supports up to 4 GiB (2^20 pages × 4 KiB).
 */
#include "ark/types.h"
#include "ark/arch.h"
#include "ark/panic.h"
#include "hw/pmm.h"
#include "ark/printk.h"

#define MAX_FRAMES   (1024 * 1024)        /* 4 GiB / 4 KiB */
#define BITMAP_WORDS (MAX_FRAMES / 32)

static u32 bitmap[BITMAP_WORDS];
static u32 total_frames = 0;
static u32 used_frames  = 0;

extern u32 _kernel_start;
extern u32 _kernel_end;

static void bm_set(u32 frame)   { bitmap[frame/32] |=  (1u << (frame%32)); }
static void bm_clear(u32 frame) { bitmap[frame/32] &= ~(1u << (frame%32)); }
static int  bm_test(u32 frame)  { return (bitmap[frame/32] >> (frame%32)) & 1; }

void pmm_init(u32 mem_kb) {
    total_frames = (mem_kb * 1024) / PAGE_SIZE;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    /* Mark everything used initially */
    for (u32 i = 0; i < BITMAP_WORDS; i++) bitmap[i] = 0xFFFFFFFF;

    /* Free conventional memory (1 MiB to mem_kb) */
    u32 start_frame = 0x100000 / PAGE_SIZE;   /* start after first 1 MiB */
    for (u32 i = start_frame; i < total_frames; i++) bm_clear(i);

    /* Mark kernel pages as used */
    u32 ks = (u32)&_kernel_start / PAGE_SIZE;
    u32 ke = ((u32)&_kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u32 i = ks; i <= ke; i++) bm_set(i);

    used_frames = ke - ks + 1 + start_frame;
    printk("PMM (32-bit): %u MiB total, %u frames\n",
           mem_kb / 1024, total_frames);
}

phys_addr_t pmm_alloc_frame(void) {
    for (u32 i = 0; i < total_frames; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            used_frames++;
            return (phys_addr_t)(i * PAGE_SIZE);
        }
    }
    kernel_panic("PMM: out of physical memory");
    return 0;
}

void pmm_free_frame(phys_addr_t addr) {
    u32 frame = (u32)addr / PAGE_SIZE;
    if (bm_test(frame)) {
        bm_clear(frame);
        used_frames--;
    }
}

u32 pmm_free_frames(void)  { return total_frames - used_frames; }
u32 pmm_total_frames(void) { return total_frames; }
