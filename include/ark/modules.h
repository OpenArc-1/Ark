/**
 * Multiboot Module Loader for Ark
 *
 * Reads modules provided by a Multiboot-compatible bootloader (like QEMU)
 * and registers them into the ramfs filesystem.
 */

#pragma once

#include "ark/types.h"
#include "ark/multiboot.h"

/**
 * Multiboot module structure
 */
typedef struct {
    u32 mod_start;      /* Start address of module */
    u32 mod_end;        /* End address of module */
    u32 string;         /* Module command line (name/path) */
    u32 reserved;
} multiboot_module_t;

/**
 * Load all modules from Multiboot info into ramfs
 * @param mbi Pointer to multiboot_info_t structure
 * @return Number of modules loaded
 */
u32 modules_load_from_multiboot(multiboot_info_t *mbi);
