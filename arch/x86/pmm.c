#include <stdint.h>
#include <stddef.h>
#include "ark/panic.h"
#include "hw/pmm.h"

typedef uint32_t u32;

#define PAGE_SIZE 4096
#define BLOCKS_PER_BUCKET 32
// Support for up to 4GB of RAM (1048576 pages)
#define MAX_BLOCKS 1048576
#define BITMAP_SIZE (MAX_BLOCKS / BLOCKS_PER_BUCKET)

/* --- PMM Globals --- */
u32 pmm_bitmap[BITMAP_SIZE];
u32 used_blocks = 0;
u32 max_blocks = 0;

/* --- External Linker Symbols --- */
// These define where your kernel lives in RAM
extern u32 _kernel_start;
extern u32 _kernel_end;

/* --- Internal Helpers --- */

static inline void bitmap_set(u32 bit) {
    pmm_bitmap[bit / 32] |= (1 << (bit % 32));
}

static inline void bitmap_unset(u32 bit) {
    pmm_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

static inline int bitmap_test(u32 bit) {
    return pmm_bitmap[bit / 32] & (1 << (bit % 32));
}

/* --- Public API --- */

/**
 * Marks a range of memory as "Used" (e.g., for the kernel or BIOS reserved areas)
 */
void pmm_init_region(u32 base, u32 size) {
    u32 align = base / PAGE_SIZE;
    u32 blocks = size / PAGE_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_set(align++);
        used_blocks++;
    }
}

/**
 * Marks a range of memory as "Free"
 */
void pmm_deinit_region(u32 base, u32 size) {
    u32 align = base / PAGE_SIZE;
    u32 blocks = size / PAGE_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_unset(align++);
        used_blocks--;
    }
}

/**
 * Allocates a single 4KB frame of physical memory
 */
void* pmm_alloc_frame() {
    for (u32 i = 0; i < BITMAP_SIZE; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) { // Optimization: skip if all 32 bits are 1
            for (u32 j = 0; j < 32; j++) {
                u32 bit = 1 << j;
                if (!(pmm_bitmap[i] & bit)) {
                    u32 frame_idx = (i * 32) + j;
                    bitmap_set(frame_idx);
                    used_blocks++;
                    return (void*)(frame_idx * PAGE_SIZE);
                }
            }
        }
    }
    return NULL; // Kernel Panic: Out of physical memory
}

/**
 * Frees a previously allocated 4KB frame
 */
void pmm_free_frame(void* addr) {
    u32 frame_idx = (u32)addr / PAGE_SIZE;
    bitmap_unset(frame_idx);
    used_blocks--;
}

/**
 * Main Setup: Called during kernel bootstrap
 * mem_size_kb: Total system RAM in Kilobytes
 */
void pmm_init(u32 mem_size_kb) {
    max_blocks = mem_size_kb / 4; // 4KB per block
    
    // 1. Default to everything "Used" (Reserved)
    // We do this for safety so we don't accidentally allocate BIOS memory
    for (u32 i = 0; i < BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    // 2. Free the actual available RAM 
    // Usually starting from 1MB to avoid BIOS/VGA junk
    pmm_deinit_region(0x100000, (mem_size_kb * 1024) - 0x100000);

    // 3. Re-reserve the kernel itself!
    u32 kernel_size = (u32)&_kernel_end - (u32)&_kernel_start;
    pmm_init_region((u32)&_kernel_start, kernel_size);
}
