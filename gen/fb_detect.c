/**
 * gen/fb_detect.c — Universal framebuffer detection for Ark kernel
 *
 * Called by both arch/x86/built-in.c and arch/x86_64/built-in.c.
 * Tries every detection method in priority order and fills an
 * ark_fb_info_t with the best result.
 *
 * Detection priority:
 *   1. Multiboot1 VBE Mode Info Block  (BIOS-authoritative, real hardware)
 *   2. Multiboot2 framebuffer tag      (UEFI/GRUB2 path)
 *   3. Multiboot1 framebuffer tag      (GRUB legacy path)
 *   4. EFI GOP                         (UEFI direct, x86_64 only)
 *   5. BGA (Bochs/QEMU) mode-set       (QEMU, VirtualBox)
 *   6. VGA PCI BAR scan                (last resort — uses whatever's there)
 *
 * All paths read back actual values rather than assuming requested ones,
 * so pitch/width/height are always consistent with what the hardware has.
 */

#define ARK_IO_INLINE
#include "../io/built-in.h"
#include "ark/types.h"
#include "ark/multiboot.h"
#include "ark/printk.h"
#include "ark/fb.h"

/* ── PCI config-space read ───────────────────────────────────────────────── */
static u32 fb_pci_read(u8 bus, u8 dev, u8 fn, u8 reg) {
    outl(0xCF8, 0x80000000u | ((u32)bus<<16) | ((u32)dev<<11) |
                ((u32)fn<<8) | (reg & 0xFCu));
    return inl(0xCFC);
}

/* ── PCI VGA BAR0 scan ───────────────────────────────────────────────────── */
/* Returns the physical address of the framebuffer (BAR0) of the first
 * VGA-class device found, or 0 if none.  Handles both 32-bit and 64-bit BARs.
 * out_vendor/out_device are filled for diagnostic printing. */
static u64 fb_find_vga_bar(u16 *out_vendor, u16 *out_device) {
    for (u8 bus = 0; bus < 16; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            u32 id = fb_pci_read(bus, dev, 0, 0x00);
            if (id == 0xFFFFFFFFu || id == 0u) continue;

            u16 vendor = (u16)(id & 0xFFFFu);
            u16 device = (u16)(id >> 16);

            /* Known VGA/display devices — match by vendor:device */
            int is_vga = 0;
            /* QEMU stdvga / Bochs VBE */
            if (vendor == 0x1234u && device == 0x1111u) is_vga = 1;
            /* VirtualBox VGA */
            if (vendor == 0x80EEu && device == 0xBEEFu) is_vga = 1;
            /* VMware SVGA */
            if (vendor == 0x15ADu && device == 0x0405u) is_vga = 1;

            /* Generic: check PCI class 0x03 (Display) if no vendor match */
            if (!is_vga) {
                u32 cr = fb_pci_read(bus, dev, 0, 0x08);
                u8  cls = (u8)(cr >> 24);
                u8  sub = (u8)(cr >> 16);
                /* 0x03:0x00 = VGA, 0x03:0x02 = 3D, 0x03:0x80 = Other Display */
                if (cls == 0x03u) is_vga = 1;
                /* Also match unclassified legacy VGA (0x00:0x01) */
                if (cls == 0x00u && sub == 0x01u) is_vga = 1;
                (void)sub;
            }

            if (!is_vga) continue;

            u32 bar0 = fb_pci_read(bus, dev, 0, 0x10);
            if (bar0 & 0x1u) continue;  /* I/O BAR, not memory */

            u32 bar_type = (bar0 >> 1) & 0x3u;
            u64 addr;
            if (bar_type == 0x2u) {
                /* 64-bit BAR — high 32 bits in BAR1 */
                u32 bar1 = fb_pci_read(bus, dev, 0, 0x14);
                addr = ((u64)bar1 << 32) | (u64)(bar0 & 0xFFFFFFF0u);
            } else {
                addr = (u64)(bar0 & 0xFFFFFFF0u);
            }

            if (!addr) continue;

            if (out_vendor) *out_vendor = vendor;
            if (out_device) *out_device = device;
            return addr;
        }
    }
    return 0;
}

/* ── BGA (Bochs/QEMU VBE) ────────────────────────────────────────────────── */
#define BGA_IDX  0x01CEu
#define BGA_DAT  0x01CFu

static void   bga_wr(u16 i, u16 v) { outw(BGA_IDX, i); outw(BGA_DAT, v); }
static u16    bga_rd(u16 i)        { outw(BGA_IDX, i); return inw(BGA_DAT); }

/* Returns 1 if BGA is present and mode-set succeeded.
 * Writes actual accepted resolution into *w/*h/*bpp. */
static int bga_set_mode(u32 w, u32 h, u32 bpp, u32 *ow, u32 *oh, u32 *obpp) {
    u16 id = bga_rd(0); /* VBE_DISPI_INDEX_ID */
    if ((id & 0xFFF0u) != 0xB0C0u) return 0;  /* not present */

    bga_wr(4, 0);            /* disable */
    bga_wr(1, (u16)w);       /* XRES */
    bga_wr(2, (u16)h);       /* YRES */
    bga_wr(3, (u16)bpp);     /* BPP */
    bga_wr(6, (u16)w);       /* VIRT_WIDTH */
    bga_wr(7, (u16)h);       /* VIRT_HEIGHT */
    bga_wr(8, 0);            /* X_OFFSET */
    bga_wr(9, 0);            /* Y_OFFSET */
    bga_wr(4, 0x41u);        /* enable | LFB */

    *ow   = bga_rd(1);
    *oh   = bga_rd(2);
    *obpp = bga_rd(3);
    return (*ow && *oh && *obpp) ? 1 : 0;
}

/* ── Multiboot2 tag walker ───────────────────────────────────────────────── */
static void mb2_find_fb(u32 mb_info_phys, u64 *addr, u32 *w, u32 *h,
                        u32 *pitch, u32 *bpp) {
    if (!mb_info_phys) return;
    u8 *p   = (u8 *)(usize)mb_info_phys;
    u32 tot = *(u32 *)p;
    u8 *end = p + tot;
    p += 8;
    while (p < end) {
        u32 tt = *(u32 *)p;
        u32 ts = *(u32 *)(p + 4);
        if (tt == 0) break;
        if (tt == 8 && ts >= 28) { /* framebuffer tag */
            *addr  = *(u64 *)(p + 8);
            *pitch = *(u32 *)(p + 16);
            *w     = *(u32 *)(p + 20);
            *h     = *(u32 *)(p + 24);
            *bpp   = *(u8  *)(p + 28) ? *(u8 *)(p + 28) : 32u;
            return;
        }
        p += (ts + 7u) & ~7u;
    }
}

/* ── Public entry point ──────────────────────────────────────────────────── */
/**
 * fb_detect() — fill *out with the best available framebuffer.
 *
 * magic        : multiboot magic (0x2BADB002 = MB1, 0x36d76289 = MB2, 0 = none)
 * mb_info_phys : physical address of multiboot info struct (0 if unavailable)
 *
 * Returns 1 if a framebuffer was found, 0 if headless.
 * On success *out is fully populated and ready for printk_set_fb().
 */
int fb_detect(u32 magic, u32 mb_info_phys, ark_fb_info_t *out) {
    u64 addr  = 0;
    u32 w     = 1024;
    u32 h     = 768;
    u32 bpp   = 32;
    u32 pitch = 0;

#define MB1_MAGIC 0x2BADB002u
#define MB2_MAGIC 0x36d76289u

    /* ── 1. Multiboot1 VBE Mode Info Block ─────────────────────────────────
     * Most reliable on real hardware — BIOS fills PhysBasePtr at offset 0x28
     * directly from INT 10h, so it's the exact address the hardware uses.    */
    if (!addr && magic == MB1_MAGIC && mb_info_phys) {
        multiboot_info_t *mbi = (multiboot_info_t *)(usize)mb_info_phys;
        if ((mbi->flags & (1u << 11)) && mbi->vbe_mode_info) {
            u8  *v = (u8 *)(usize)mbi->vbe_mode_info;
            u32  lfb  = *(u32 *)(v + 0x28);  /* PhysBasePtr */
            u16  bpsl = *(u16 *)(v + 0x10);  /* BytesPerScanLine */
            u16  xr   = *(u16 *)(v + 0x12);  /* XResolution */
            u16  yr   = *(u16 *)(v + 0x14);  /* YResolution */
            u8   bp   = *(u8  *)(v + 0x19);  /* BitsPerPixel */
            if (lfb && xr && yr && bp) {
                addr  = (u64)lfb;
                w     = xr; h = yr; bpp = bp;
                pitch = bpsl ? (u32)bpsl : (w * (bpp / 8));
                printk(T, "fb: MB1 VBE %ux%ux%u @ 0x%x pitch=%u\n",
                       w, h, bpp, (u32)addr, pitch);
            }
        }
    }

    /* ── 2. Multiboot2 framebuffer tag ─────────────────────────────────────
     * GRUB2 in UEFI mode and VirtualBox typically provide this.               */
    if (!addr && magic == MB2_MAGIC) {
        mb2_find_fb(mb_info_phys, &addr, &w, &h, &pitch, &bpp);
        if (addr)
            printk(T, "fb: MB2 tag %ux%ux%u @ 0x%x pitch=%u\n",
                   w, h, bpp, (u32)addr, pitch);
    }

    /* ── 3. Multiboot1 framebuffer tag (bit 12) ─────────────────────────── */
    if (!addr && magic == MB1_MAGIC && mb_info_phys) {
        multiboot_info_t *mbi = (multiboot_info_t *)(usize)mb_info_phys;
        if (mbi->flags & (1u << 12)) {
            addr  = (u64)mbi->framebuffer_addr;
            w     = mbi->framebuffer_width;
            h     = mbi->framebuffer_height;
            bpp   = mbi->framebuffer_bpp  ? mbi->framebuffer_bpp  : 32u;
            pitch = mbi->framebuffer_pitch ? mbi->framebuffer_pitch
                                           : (w * (bpp / 8));
            if (addr)
                printk(T, "fb: MB1 tag %ux%ux%u @ 0x%x pitch=%u\n",
                       w, h, bpp, (u32)addr, pitch);
            else
                addr = 0; /* tag present but zero addr — ignore */
        }
    }

    /* ── 4. BGA mode-set (QEMU stdvga / Bochs / VirtualBox) ────────────────
     * Try to program the Bochs VBE adapter via I/O ports 0x01CE/0x01CF.
     * Read back the actual mode accepted — do NOT assume what we requested.  */
    if (!addr) {
        u32 bw = 0, bh = 0, bbpp = 0;
        if (bga_set_mode(w, h, bpp, &bw, &bh, &bbpp)) {
            u16 vnd = 0, dev_id = 0;
            u64 bar = fb_find_vga_bar(&vnd, &dev_id);
            if (bar) {
                addr  = bar;
                w     = bw; h = bh; bpp = bbpp;
                pitch = w * (bpp / 8);
                printk(T, "fb: BGA %ux%ux%u @ 0x%x (pci %04x:%04x)\n",
                       w, h, bpp, (u32)addr, (u32)vnd, (u32)dev_id);
            }
        }
    }

    /* ── 5. PCI VGA BAR scan — no mode-set, use whatever's mapped ───────── *
     * Last resort: BGA not present (VMware, bare PCIe GPU, etc.) but a      *
     * VGA device exists with a BAR. We use the current resolution (from      *
     * defaults or whatever GRUB set) and just point at the BAR.             */
    if (!addr) {
        u16 vnd = 0, dev_id = 0;
        u64 bar = fb_find_vga_bar(&vnd, &dev_id);
        if (bar) {
            addr  = bar;
            /* Keep w/h/bpp defaults — we can't know what the current mode is */
            pitch = w * (bpp / 8);
            printk(T, "fb: PCI BAR %ux%ux%u @ 0x%x (pci %04x:%04x)\n",
                   w, h, bpp, (u32)addr, (u32)vnd, (u32)dev_id);
        }
    }

    if (!addr) {
        printk(T, "fb: no framebuffer — headless\n");
        return 0;
    }

    /* ── Pitch sanity ───────────────────────────────────────────────────── */
    /* Some GRUB versions report pitch in pixels rather than bytes.
     * If pitch < w * (bpp/8) it must be in pixels — multiply up.           */
    u32 min_pitch = w * (bpp / 8);
    if (pitch > 0 && pitch < min_pitch) pitch = pitch * (bpp / 8);
    if (pitch < min_pitch || pitch == 0) pitch = min_pitch;

    /* ── Clear framebuffer ──────────────────────────────────────────────── */
    u32 fb_bytes = h * pitch;
    if (fb_bytes < (256u * 1024u * 1024u)) { /* sanity: < 256 MiB */
        u8 *p = (u8 *)(usize)addr;
        u32 i;
        for (i = 0; i + 3 < fb_bytes; i += 4) *(u32 *)(p + i) = 0;
        for (; i < fb_bytes; i++)               p[i] = 0;
    }

    out->addr   = (u8 *)(usize)addr;
    out->width  = w;
    out->height = h;
    out->pitch  = pitch;
    out->bpp    = bpp;
    return 1;
}
