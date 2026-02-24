/**
 * Minimal Multiboot v1 structures for framebuffer access.
 *
 * This is enough to read the linear framebuffer information that a
 * Multiboot-compliant bootloader (or QEMU's multiboot loader) passes
 * to the kernel.
 */

#pragma once

#include "ark/types.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

typedef struct multiboot_info {
    u32 flags;

    /* Memory info. */
    u32 mem_lower;
    u32 mem_upper;

    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;

    /* ELF section header table or a.out symbol table. */
    u32 num;
    u32 size;
    u32 addr;
    u32 shndx;

    /* Memory map. */
    u32 mmap_length;
    u32 mmap_addr;

    /* Drive info. */
    u32 drives_length;
    u32 drives_addr;

    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;

    /* VBE */
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;

    /* Framebuffer */
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8  framebuffer_bpp;
    u8  framebuffer_type;
    u8  reserved[2];
} __attribute__((packed)) multiboot_info_t;

