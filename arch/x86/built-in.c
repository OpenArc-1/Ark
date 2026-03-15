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

#define MB_MAGIC   0x1BADB002u
/* bit 0 = mem_info required.
 * bit 2 = video mode request — INTENTIONALLY OMITTED.
 * QEMU's multiboot loader crashes or rejects kernels that set bit 2.
 * GRUB on real hardware will still provide a framebuffer via its own
 * video negotiation (set gfxmode in grub.cfg) regardless of this bit.
 * The video hint fields below are inert when bit 2 is not set. */
#define MB_FLAGS   0x00000001u   /* bit0 (mem_info) only */

__attribute__((section(".multiboot"), used))
static const unsigned int multiboot_header[8] = {
    MB_MAGIC,
    MB_FLAGS,
    (unsigned)(-(MB_MAGIC + MB_FLAGS)),  /* checksum */
    0, 0, 0, 0, 0,                       /* load address fields (0 = ELF) */
};

/* Use inline I/O — avoids duplicate symbols with io/built-in.c */
#define ARK_IO_INLINE
#include "../io/built-in.h"
#include "ark/types.h"
#include "ark/multiboot.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/modules.h"
#include "ark/ramfs.h"
#include "ark/fb.h"
#include "gpu/vesa.h"
#include "hw/pmm.h"

void kernel_main(void);
ark_fb_info_t g_fb_info;

extern void printk_set_fb(u32 *addr, u32 width, u32 height, u32 pitch, u32 bpp);

/* ── PCI config access ─────────────────────────────────────────── */

static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 reg) {
    outl(0xCF8, 0x80000000u | ((u32)bus<<16) | ((u32)dev<<11) |
                ((u32)fn<<8) | (reg & 0xFC));
    return inl(0xCFC);
}

/* Find VGA framebuffer BAR0.
 *
 * Handles 64-bit prefetchable BARs (BAR type bits[2:1] = 10):
 *   BAR0[31:4] = low 32 bits of address, BAR1 = high 32 bits.
 *   A 32-bit kernel can only use the framebuffer if high bits are 0.
 *
 * Uses PCI class code 0x03 (Display Controller) for generic GPU detection
 * instead of relying solely on vendor IDs.
 */
static u32 find_vga_bar0(void) {
    for (u8 bus = 0; bus < 16; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            u32 id = pci_read(bus, dev, 0, 0);
            if (id == 0xFFFFFFFF) continue;

            u16 vendor    = id & 0xFFFF;
            u16 device_id = (id >> 16) & 0xFFFF;
            int is_vga    = 0;

            /* QEMU stdvga (1234:1111) — match by ID first */
            if (id == 0x11111234u) {
                is_vga = 1;
            } else {
                /* Match any PCI Display Controller (base class 0x03) */
                u32 cls = pci_read(bus, dev, 0, 0x08);
                if (((cls >> 24) & 0xFF) == 0x03) {
                    printk(T, "pci: display %04x:%04x on %u:%u\n",
                           vendor, device_id, bus, dev);
                    is_vga = 1;
                }
            }

            if (!is_vga) continue;

            u32 bar0 = pci_read(bus, dev, 0, 0x10);
            if (bar0 & 0x01) continue;          /* I/O BAR — skip */

            /* BAR memory type: bits[2:1]  00=32-bit  10=64-bit */
            u32 bar_type = (bar0 >> 1) & 0x03;
            u32 addr     = bar0 & 0xFFFFFFF0u;

            if (bar_type == 0x02) {
                /* 64-bit BAR: high 32 bits in the next BAR slot (offset 0x14) */
                u32 bar1 = pci_read(bus, dev, 0, 0x14);
                if (bar1 != 0) {
                    /* Physical address > 4 GiB — unreachable from 32-bit mode */
                    printk(T, "pci: VGA BAR64 above 4GiB (hi=0x%x), skip\n", bar1);
                    continue;
                }
            }

            if (addr) {
                printk(T, "pci: VGA bar0=0x%x (type=%u)\n", addr, bar_type);
                return addr;
            }
        }
    }
    return 0;
}

/* ── BGA (Bochs VBE) port access ───────────────────────────────── */
#define BGA_IDX  0x01CE
#define BGA_DAT  0x01CF

/* BGA register indices */
#define BGA_REG_ID          0
#define BGA_REG_XRES        1
#define BGA_REG_YRES        2
#define BGA_REG_BPP         3
#define BGA_REG_ENABLE      4
#define BGA_REG_BANK        5
#define BGA_REG_VIRT_WIDTH  6
#define BGA_REG_VIRT_HEIGHT 7
#define BGA_REG_X_OFFSET    8
#define BGA_REG_Y_OFFSET    9

#define BGA_ENABLED         0x01
#define BGA_LFB_ENABLED     0x40

static void bga_write(u16 idx, u16 val) {
    outw(BGA_IDX, idx); outw(BGA_DAT, val);
}
static u16 bga_read(u16 idx) {
    outw(BGA_IDX, idx); return inw(BGA_DAT);
}

/* Set resolution via BGA (Bochs VBE / QEMU stdvga).
 * Returns 1 if mode was accepted, 0 if BGA not present.
 * On return, *out_w/*out_h/*out_bpp contain the values read back. */
static int bga_set_mode(u32 w, u32 h, u32 bpp,
                        u32 *out_w, u32 *out_h, u32 *out_bpp) {
    /* BGA ID register: 0xB0C0–0xB0CF for known versions.
     * Log whatever we get so it shows on serial if detection fails. */
    u16 id = bga_read(BGA_REG_ID);
    printk(T, "fb: BGA id=0x%04x\n", id);

    /* Accept 0xB0C0–0xB0CF (Bochs VBE versions 0–15).
     * Some QEMU builds with -vga std also respond with 0xB0C4 or 0xB0C5.
     * Reject 0x0000 and 0xFFFF which mean the port isn't mapped at all. */
    if (id == 0x0000 || id == 0xFFFF) return 0;
    if ((id & 0xFF00u) != 0xB000u)    return 0;

    /* Disable display while changing mode */
    bga_write(BGA_REG_ENABLE, 0);

    bga_write(BGA_REG_XRES,        (u16)w);
    bga_write(BGA_REG_YRES,        (u16)h);
    bga_write(BGA_REG_BPP,         (u16)bpp);
    bga_write(BGA_REG_VIRT_WIDTH,  (u16)w);
    bga_write(BGA_REG_VIRT_HEIGHT, (u16)h);
    bga_write(BGA_REG_X_OFFSET,    0);
    bga_write(BGA_REG_Y_OFFSET,    0);

    /* Enable with LFB */
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

    /* Read back what was actually accepted */
    *out_w   = bga_read(BGA_REG_XRES);
    *out_h   = bga_read(BGA_REG_YRES);
    *out_bpp = bga_read(BGA_REG_BPP);

    /* Sanity — if readback is 0, mode-set failed */
    if (!*out_w || !*out_h || !*out_bpp) return 0;
    return 1;
}

void arch_x86_entry(u32 magic, u32 mb_info) {
    ramfs_init();

    /* Accept both Multiboot1 (0x2BADB002) and Multiboot2 (0x36d76289) */
#define MB1_MAGIC 0x2BADB002u
#define MB2_MAGIC 0x36d76289u
    if (magic != MB1_MAGIC && magic != MB2_MAGIC) {
        kernel_main();
        for (;;) __asm__("hlt");
    }

    multiboot_info_t *mbi = (multiboot_info_t *)(u32)mb_info;

    /* ── Initialize physical memory manager from Multiboot memory map ── */
    pmm_init_from_multiboot(mbi);

    /* ── Parse Multiboot memory map if available ── */
    if (mbi->flags & (1u << 6)) {  /* bit 6 = mmap_addr */
        multiboot_mmap_entry_t *mmap = (multiboot_mmap_entry_t *)mbi->mmap_addr;
        u32 mmap_end = mbi->mmap_addr + mbi->mmap_length;
        u32 total_mem = 0;
        
        while ((u32)mmap < mmap_end) {
            if (mmap->type == 1 && mmap->length > 0) {  /* Available RAM */
                total_mem += mmap->length / 1024;
            }
            mmap = (multiboot_mmap_entry_t *)((u32)mmap + mmap->size + sizeof(mmap->size));
        }
        
        if (total_mem > 0)
            printk(T, "mem: %u MiB\n", total_mem / 1024);
    }

    /* ── Framebuffer setup ── */
    u32 fb_w = 1024, fb_h = 768, fb_bpp = 32, fb_addr = 0, fb_pitch = 0;

    /* VBE Mode Info Block layout (VESA VBE 2.0, 256 bytes at mbi->vbe_mode_info):
     *   0x10  u16  BytesPerScanLine  — actual pitch in bytes
     *   0x12  u16  XResolution
     *   0x14  u16  YResolution
     *   0x19  u8   BitsPerPixel
     *   0x28  u32  PhysBasePtr       — BIOS-programmed LFB physical address
     *
     * This is the most reliable source on real hardware because the BIOS
     * fills it directly via INT 10h/AX=4F01h. GRUB copies it into the
     * multiboot info struct but can truncate the 32-bit address on some
     * firmware. Reading it straight from the BIOS block avoids that. */

    /* 1. VBE Mode Info Block — authoritative BIOS address (bit 11 set) */
    if ((mbi->flags & (1u << 11)) && mbi->vbe_mode_info) {
        u8 *vmi  = (u8 *)(u32)mbi->vbe_mode_info;
        u32 lfb  = *(u32 *)(vmi + 0x28);    /* PhysBasePtr */
        u16 bpsl = *(u16 *)(vmi + 0x10);    /* BytesPerScanLine */
        u16 xres = *(u16 *)(vmi + 0x12);    /* XResolution */
        u16 yres = *(u16 *)(vmi + 0x14);    /* YResolution */
        u8  bpp  = *(u8  *)(vmi + 0x19);    /* BitsPerPixel */
        if (lfb && xres && yres && bpp) {
            fb_addr  = lfb;
            fb_w     = xres;
            fb_h     = yres;
            fb_bpp   = bpp;
            fb_pitch = bpsl ? (u32)bpsl : (fb_w * (fb_bpp / 8));
            printk(T, "fb: VBE BIOS LFB %ux%ux%u @ 0x%x pitch=%u\n",
                   fb_w, fb_h, fb_bpp, fb_addr, fb_pitch);
        }
    }

    /* 2. Multiboot framebuffer tag (bit 12) — use if VBE block missing/zero */
    if (!fb_addr && (mbi->flags & (1u << 12))) {
        fb_addr  = (u32)mbi->framebuffer_addr;
        fb_w     = mbi->framebuffer_width;
        fb_h     = mbi->framebuffer_height;
        fb_bpp   = mbi->framebuffer_bpp;
        fb_pitch = mbi->framebuffer_pitch ? mbi->framebuffer_pitch
                                          : (fb_w * (fb_bpp / 8));
        printk(T, "fb: multiboot tag %ux%ux%u @ 0x%x pitch=%u\n",
               fb_w, fb_h, fb_bpp, fb_addr, fb_pitch);
    }

    /* 3. BGA mode-set + PCI BAR scan (QEMU stdvga / Bochs) */
    if (!fb_addr) {
        u32 bga_w = 0, bga_h = 0, bga_bpp = 0;
        int bga_ok = bga_set_mode(1024, 768, 32, &bga_w, &bga_h, &bga_bpp);
        fb_addr = find_vga_bar0();
        if (fb_addr && bga_ok) {
            /* Use the resolution BGA actually accepted — may differ from
             * what we requested if QEMU has limited VRAM or mode support. */
            fb_w     = bga_w;
            fb_h     = bga_h;
            fb_bpp   = bga_bpp;
            fb_pitch = fb_w * (fb_bpp / 8);
            printk(T, "fb: BGA %ux%ux%u @ 0x%x\n", fb_w, fb_h, fb_bpp, fb_addr);
        } else if (fb_addr) {
            /* BGA not responding — QEMU running with -vga none or similar.
             * Assume 640x480x32 (QEMU's safe default) so pitch is correct. */
            fb_w     = 640;
            fb_h     = 480;
            fb_bpp   = 32;
            fb_pitch = fb_w * 4;
            printk(T, "fb: VGA fallback 640x480x32 @ 0x%x\n", fb_addr);
        }
        /* No hardcoded address — run headless if no BAR found */
    }

    if (fb_addr && fb_w > 0 && fb_h > 0 && fb_bpp > 0) {
        u32 fb_size = fb_h * fb_pitch;

        /* Safely clear framebuffer (check for reasonable size) */
        if (fb_size < (128 * 1024 * 1024)) {  /* Less than 128 MiB */
            u32 *p = (u32 *)fb_addr;
            for (u32 i = 0; i < (fb_size / 4); i++) p[i] = 0;
        }

        printk_set_fb((u32 *)fb_addr, fb_w, fb_h, fb_pitch, fb_bpp);

        /* Real-hardware fix: clear VGA Sequencer Screen-Off bit.
         * BIOS/UEFI sometimes leaves bit 5 of Sequencer reg 1 set,
         * causing a blank display ~1 minute after boot (same bug Linux
         * fixed in fbcon). Do this immediately after FB is live. */
        { extern void display_vga_unblank(void); display_vga_unblank(); }

        g_fb_info.addr   = (u8 *)fb_addr;
        g_fb_info.pitch  = fb_pitch;
        g_fb_info.width  = fb_w;
        g_fb_info.height = fb_h;
        g_fb_info.bpp    = fb_bpp;

        vesa_init(fb_addr, fb_w, fb_h, fb_pitch, fb_bpp);

        printk(T, "fb: %ux%ux%u  pitch=%u  fb=0x%x\n", fb_w, fb_h, fb_bpp, fb_pitch, fb_addr);
    } else {
        printk(T, "fb: no framebuffer — headless\n");
    }

    /* ── initramfs modules ── */
    if ((mbi->flags & 0x08u) && mbi->mods_count > 0) {
        u32 n = modules_load_from_multiboot(mbi);
        printk(T, "initramfs: %u file(s)\n", n);
    }

    kernel_main();
    kernel_panic("kernel_main returned");
}
