/**
 * arch/x86/pmm.c — 32-bit Physical Memory Manager for Ark kernel
 *
 * Bitmap allocator: each bit represents one 4 KiB page frame.
 * Supports up to 4 GiB (2^20 pages × 4 KiB).
 * Reads memory map from Multiboot for accurate available RAM detection.
 */
#include "ark/types.h"
#include "ark/arch.h"
#include "ark/panic.h"
#include "ark/multiboot.h"
#include "hw/pmm.h"
#include "ark/printk.h"

#define MAX_FRAMES   (1024 * 1024)        /* 4 GiB / 4 KiB */
#define BITMAP_WORDS (MAX_FRAMES / 32)

static u32 bitmap[BITMAP_WORDS];
static u32 total_frames = 0;
static u32 used_frames  = 0;

extern char _kernel_start[];
extern char _kernel_end[];

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
    u32 ks = (u32)_kernel_start / PAGE_SIZE;
    u32 ke = ((u32)_kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u32 i = ks; i <= ke; i++) bm_set(i);

    used_frames = ke - ks + 1 + start_frame;
    printk(T, "pmm (32-bit): %u MiB total, %u frames available\n",
           mem_kb / 1024, total_frames - used_frames);

    printk("[PMM] Memory layout:\n");
    printk("  KERNEL: virt 0x%08x - 0x%08x (size: %u KB)\n",
           (u32)_kernel_start, (u32)_kernel_end,
           ((u32)_kernel_end - (u32)_kernel_start) / 1024);
    printk("  BITMAP: virt 0x%08x (size: %u bytes)\n",
           (u32)bitmap, BITMAP_WORDS * 4);
    printk("  FRAMES: total=%u used=%u free=%u\n",
           total_frames, used_frames, total_frames - used_frames);
}

void pmm_init_from_multiboot(multiboot_info_t *mbi) {
    if (!(mbi->flags & (1u << 6))) {
        /* No memory map available, use basic init */
        pmm_init(mbi->mem_upper);
        return;
    }

    /* Parse Multiboot memory map for accurate memory detection */
    multiboot_mmap_entry_t *mmap = (multiboot_mmap_entry_t *)mbi->mmap_addr;
    u32 mmap_end = mbi->mmap_addr + mbi->mmap_length;
    u32 max_mem = 0;

    /* First pass: find the highest available RAM to set total_frames */
    while ((u32)mmap < mmap_end) {
        if (mmap->type == 1) {  /* Available RAM */
            u64 end = mmap->addr + mmap->length;
            if (end < 0x100000000ULL) {  /* Only for 32-bit addressable */
                u32 end_addr = (u32)end;
                if (end_addr > max_mem) max_mem = end_addr;
            }
        }
        mmap = (multiboot_mmap_entry_t *)((u32)mmap + mmap->size + sizeof(mmap->size));
    }

    total_frames = max_mem / PAGE_SIZE;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    /* Mark everything used initially */
    for (u32 i = 0; i < BITMAP_WORDS; i++) bitmap[i] = 0xFFFFFFFF;

    /* Second pass: mark available regions */
    mmap = (multiboot_mmap_entry_t *)mbi->mmap_addr;
    while ((u32)mmap < mmap_end) {
        if (mmap->type == 1) {  /* Available RAM */
            u32 start = mmap->addr / PAGE_SIZE;
            u32 length = mmap->length / PAGE_SIZE;
            for (u32 i = 0; i < length && start + i < total_frames; i++) {
                bm_clear(start + i);
            }
        }
        mmap = (multiboot_mmap_entry_t *)((u32)mmap + mmap->size + sizeof(mmap->size));
    }

    /* Mark kernel pages as used */
    u32 ks = (u32)_kernel_start / PAGE_SIZE;
    u32 ke = ((u32)_kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u32 i = ks; i <= ke; i++) bm_set(i);

    used_frames = ke - ks + 1;
    printk(T, "pmm (32-bit): detected %u MiB total, %u frames available\n",
           max_mem / (1024 * 1024), total_frames - used_frames);

    printk("[PMM] Multiboot memory map parsed:\n");
    printk("  KERNEL: virt %p - %p\n",
           (void*)(usize)(u32)_kernel_start, (void*)(usize)(u32)_kernel_end);
    printk("  BITMAP: virt %p (size: %u bytes)\n",
           (void*)(usize)bitmap, BITMAP_WORDS * 4);
    printk("  FRAMES: total=%u used=%u free=%u\n",
           total_frames, used_frames, total_frames - used_frames);
}

phys_addr_t pmm_alloc_frame(void) {
    for (u32 i = 0; i < total_frames; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            used_frames++;
            phys_addr_t addr = (phys_addr_t)(i * PAGE_SIZE);
#if CONFIG_PRINTK_ENABLE
            static int alloc_count = 0;
            if(alloc_count++ % 256 == 0){
                printk("[PMM] alloc: phys=%p frame=%u free=%u\n",
                       (void*)(usize)addr, i, total_frames - used_frames);
            }
#endif
            return addr;
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
#if CONFIG_PRINTK_ENABLE
        static int free_count = 0;
        if(free_count++ % 256 == 0){
            printk("[PMM] free:  phys=%p frame=%u free=%u\n",
                   (void*)(usize)addr, frame, total_frames - used_frames);
        }
#endif
    }
}

u32 pmm_free_frames(void)  { return total_frames - used_frames; }
u32 pmm_total_frames(void) { return total_frames; }
