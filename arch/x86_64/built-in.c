/**
 * arch/x86_64/built-in.c — Ark kernel x86_64 early-boot C entry
 *
 * NOTE: Define ARK_IO_INLINE so that io/built-in.h emits static-inline
 * helpers here instead of extern references.  This file is compiled as
 * part of the kernel but the linker-visible io_* symbols live in
 * io/built-in.c; using inline copies here avoids duplicate-symbol errors
 * while still providing fast inlined access to BGA/PCI ports at boot.
 */
#define ARK_IO_INLINE
#include "io/built-in.h"
#include "ark/types.h"
#include "ark/multiboot.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/modules.h"
#include "ark/ramfs.h"
#include "ark/fb.h"
#include "gpu/vesa.h"

void kernel_main(void);
void idt64_init(void);

ark_fb_info_t g_fb_info;
extern void printk_set_fb(u32 *addr, u32 width, u32 height, u32 pitch);


/* ── PCI ─────────────────────────────────────────────────────────── */
static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 reg) {
    outl(0xCF8, 0x80000000u | ((u32)bus<<16) | ((u32)dev<<11) | ((u32)fn<<8) | (reg&0xFC));
    return inl(0xCFC);
}

static u64 find_vga_bar0(void) {
    for (u8 bus = 0; bus < 8; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            u32 id = pci_read(bus, dev, 0, 0);
            if (id == 0xFFFFFFFF) continue;
            if (id == 0x11111234u) {          /* QEMU stdvga */
                u32 bar0 = pci_read(bus, dev, 0, 0x10);
                u32 type = (bar0 >> 1) & 0x3;
                if (type == 0x2) {            /* 64-bit BAR */
                    u32 bar1 = pci_read(bus, dev, 0, 0x14);
                    return ((u64)bar1 << 32) | (bar0 & 0xFFFFFFF0u);
                }
                return (u64)(bar0 & 0xFFFFFFF0u);
            }
        }
    }
    return 0;
}

/* ── BGA (Bochs/QEMU VBE) ────────────────────────────────────────── */
#define BGA_IDX 0x01CE
#define BGA_DAT 0x01CF

static void bga_write(u16 idx, u16 val) {
    outw(BGA_IDX, idx);
    outw(BGA_DAT, val);
}

static void bga_set_mode(u32 w, u32 h, u32 bpp) {
    bga_write(0, 0xB0C0);    /* ping */
    bga_write(4, 0);          /* disable */
    bga_write(1, (u16)w);
    bga_write(2, (u16)h);
    bga_write(3, (u16)bpp);
    bga_write(6, (u16)w);    /* virt width */
    bga_write(7, (u16)h);    /* virt height */
    bga_write(8, 0);
    bga_write(9, 0);
    bga_write(4, 0x41);      /* enable LFB */
}

/* ── Entry point ─────────────────────────────────────────────────── */
void arch_x86_64_entry(u32 magic, u32 mb_info_phys) {
    ramfs_init();
    idt64_init();

    /* Always try to set up BGA mode */
    bga_set_mode(1024, 768, 32);

    /* Try to find framebuffer address */
    u64 fb_addr = find_vga_bar0();

    /* If PCI scan failed, try multiboot info */
    if (!fb_addr && magic == MULTIBOOT_BOOTLOADER_MAGIC && mb_info_phys) {
        multiboot_info_t *mbi = (multiboot_info_t *)(usize)mb_info_phys;
        if (mbi->flags & (1u << 12))
            fb_addr = mbi->framebuffer_addr;
    }

    /*
     * Last resort: QEMU stdvga always maps its LFB at 0xFD000000 when
     * no other address is configured. This is safe to probe.
     */
    if (!fb_addr)
        fb_addr = 0xFD000000u;

    /* Set up framebuffer */
    {
        u32  pitch = 1024 * 4;
        u32 *fb    = (u32 *)(usize)fb_addr;

        /* Clear to black */
        for (u32 i = 0; i < 1024 * 768; i++) fb[i] = 0;

        printk_set_fb(fb, 1024, 768, pitch);

        g_fb_info.addr   = (u8 *)(usize)fb_addr;
        g_fb_info.pitch  = pitch;
        g_fb_info.width  = 1024;
        g_fb_info.height = 768;
        g_fb_info.bpp    = 32;

        vesa_init((u32)fb_addr, 1024, 768, pitch, 32);

        printk("Ark x86_64  1024x768x32  fb=0x%llx\n", fb_addr);
    }

    /* Load initramfs modules if multiboot gave us any */
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && mb_info_phys) {
        multiboot_info_t *mbi = (multiboot_info_t *)(usize)mb_info_phys;
        if ((mbi->flags & 0x08u) && mbi->mods_count > 0) {
            u32 n = modules_load_from_multiboot(mbi);
            printk("initramfs: %u file(s)\n", n);
        }
    }

    kernel_main();
    kernel_panic("kernel_main returned");
}
