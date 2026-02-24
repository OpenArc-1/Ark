/**
 * x86 Multiboot 1 entry for Ark.
 *
 * STRICT 3-WORD HEADER — no extra fields after checksum.
 * QEMU rejects any header with words after checksum unless bit 16 is set.
 * Bit 16 requires a full 8-word address block which conflicts with ELF loading.
 *
 * FRAMEBUFFER:
 *   We do NOT request a video mode. Instead QEMU is launched with:
 *     -vga std -device VGA,vgamem_mb=16
 *   and the resolution is set from protected mode by writing to the
 *   Bochs VBE (BGA) I/O ports 0x01CE / 0x01CF, which QEMU always emulates.
 *   This works without any BIOS call or multiboot video negotiation.
 *   The framebuffer physical address is found from the PCI BAR0 of the
 *   VGA device (vendor 0x1234, device 0x1111).
 */

#define MB_MAGIC 0x1BADB002u
#define MB_FLAGS 0x00000001u   /* bit 0 only: mem_info (no video request) */

__attribute__((section(".multiboot"), used))
static const unsigned int multiboot_header[3] = {
    MB_MAGIC,
    MB_FLAGS,
    (unsigned)(-(MB_MAGIC + MB_FLAGS))
};

#include "ark/types.h"
#include "ark/multiboot.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/modules.h"
#include "ark/ramfs.h"
#include "ark/fb.h"
#include "gpu/vesa.h"

void kernel_main(void);
ark_fb_info_t g_fb_info;

extern void printk_set_fb(u32 *addr, u32 width, u32 height, u32 pitch);

/* ── PCI config access ─────────────────────────────────────────── */
static inline void outl(u16 port, u32 v) {
    __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v;
}
static inline void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v;
}
static inline void outw(u16 port, u16 v) {
    __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port)); return v;
}

static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 reg) {
    outl(0xCF8, 0x80000000u | ((u32)bus<<16) | ((u32)dev<<11) |
                ((u32)fn<<8) | (reg & 0xFC));
    return inl(0xCFC);
}

/* Find QEMU stdvga (1234:1111) BAR0 = framebuffer physical base */
static u32 find_vga_bar0(void) {
    for (u8 bus = 0; bus < 4; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            u32 id = pci_read(bus, dev, 0, 0);
            if (id == 0xFFFFFFFF) continue;
            if (id == 0x11111234u) {          /* QEMU stdvga */
                u32 bar0 = pci_read(bus, dev, 0, 0x10);
                return bar0 & 0xFFFFFFF0u;    /* strip type bits */
            }
        }
    }
    return 0;
}

/* ── BGA (Bochs VBE) port access ───────────────────────────────── */
#define BGA_IDX  0x01CE
#define BGA_DAT  0x01CF

static void bga_write(u16 idx, u16 val) {
    outw(BGA_IDX, idx); outw(BGA_DAT, val);
}

/* Set resolution via BGA — works from protected mode, no BIOS needed */
static void bga_set_mode(u32 w, u32 h, u32 bpp) {
    bga_write(0, 0xB0C0);   /* VBE_DISPI_INDEX_ID — ping */
    bga_write(4, 0);         /* disable */
    bga_write(1, (u16)w);    /* XRES */
    bga_write(2, (u16)h);    /* YRES */
    bga_write(3, (u16)bpp);  /* BPP  */
    bga_write(6, (u16)w);    /* VIRT_WIDTH */
    bga_write(7, (u16)h);    /* VIRT_HEIGHT */
    bga_write(8, 0);         /* X_OFFSET */
    bga_write(9, 0);         /* Y_OFFSET */
    bga_write(4, 0x41);      /* enable | LFB */
}

void arch_x86_entry(u32 magic, u32 mb_info) {
    ramfs_init();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kernel_main();
        for (;;) __asm__("hlt");
    }

    multiboot_info_t *mbi = (multiboot_info_t *)(u32)mb_info;

    /* ── Set 1024x768x32 via BGA ── */
    bga_set_mode(1024, 768, 32);

    /* ── Find framebuffer address from PCI BAR0 ── */
    u32 fb_addr = find_vga_bar0();
    if (!fb_addr) {
        /* BAR0 scan failed — try the address multiboot may have given us */
        if (mbi->flags & (1u << 12))
            fb_addr = (u32)mbi->framebuffer_addr;
    }

    if (fb_addr) {
        u32 pitch = 1024 * 4;

        /* Clear to black */
        u32 *p = (u32 *)fb_addr;
        for (u32 i = 0; i < 1024 * 768; i++) p[i] = 0;

        printk_set_fb((u32 *)fb_addr, 1024, 768, pitch);

        g_fb_info.addr   = (u8 *)fb_addr;
        g_fb_info.pitch  = pitch;
        g_fb_info.width  = 1024;
        g_fb_info.height = 768;
        g_fb_info.bpp    = 32;

        vesa_init(fb_addr, 1024, 768, pitch, 32);

        printk("Ark  1024x768x32  fb=0x%x\n", fb_addr);
    } else {
        printk("Ark: no framebuffer found\n");
    }

    /* ── initramfs modules ── */
    if ((mbi->flags & 0x08u) && mbi->mods_count > 0) {
        u32 n = modules_load_from_multiboot(mbi);
        printk("initramfs: %u file(s)\n", n);
    }

    kernel_main();
    kernel_panic("kernel_main returned");
}
