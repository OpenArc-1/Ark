/**
 * arch/x86_64/built-in.c — Ark kernel x86_64 early-boot C entry
 *
 * Initialised by boot.S → arch_x86_64_entry().
 * Responsible for:
 *   1. IDT (exceptions + PIC IRQs + int 0x80 compat gate)
 *   2. SYSCALL/SYSRET fast path (via syscall64_install)
 *   3. Framebuffer (EFI GOP → BGA → fallback address)
 *   4. Calling kernel_main()
 */
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
#include "gpu/efi_gop.h"

extern void kernel_main(void);
extern void idt64_init(void);
extern void syscall64_install(void);   /* arch/x86_64/syscall64.S */

ark_fb_info_t g_fb_info;
extern void printk_set_fb(u32 *addr, u32 width, u32 height, u32 pitch, u32 bpp);

/* ── PCI helpers ─────────────────────────────────────────────────────────── */
static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 reg) {
    outl(0xCF8,
         0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
         ((u32)fn << 8) | (reg & 0xFC));
    return inl(0xCFC);
}

static u64 find_vga_bar0(void) {
    /* Scan PCI buses 0-7 for known VGA framebuffer devices.
     * Returns the physical address of BAR0 (linear framebuffer). */
    for (u8 bus = 0; bus < 8; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            u32 id = pci_read(bus, dev, 0, 0);
            if (id == 0xFFFFFFFF) continue;

            /* QEMU stdvga / Bochs VBE adapter */
            /* VirtualBox VGA (vendor=80EE, device=BEEF) */
            if (id == 0x11111234u || id == 0xBEEF80EEu) {
                u32 bar0 = pci_read(bus, dev, 0, 0x10);
                /* BAR type: bits [2:1] — 0x2 = 64-bit, 0x0 = 32-bit */
                u32 type = (bar0 >> 1) & 0x3;
                if (type == 0x2) {
                    u32 bar1 = pci_read(bus, dev, 0, 0x14);
                    return ((u64)bar1 << 32) | (bar0 & 0xFFFFFFF0u);
                }
                return (u64)(bar0 & 0xFFFFFFF0u);
            }

            /* Generic: any VGA-class PCI device (class=0x03, subclass=0x00) */
            u32 class_rev = pci_read(bus, dev, 0, 0x08);
            u8 class    = (u8)(class_rev >> 24);
            u8 subclass = (u8)(class_rev >> 16);
            if (class == 0x03 && subclass == 0x00) {
                u32 bar0 = pci_read(bus, dev, 0, 0x10);
                if ((bar0 & 0x1) == 0) {          /* memory BAR, not I/O */
                    u32 type = (bar0 >> 1) & 0x3;
                    if (type == 0x2) {
                        u32 bar1 = pci_read(bus, dev, 0, 0x14);
                        return ((u64)bar1 << 32) | (bar0 & 0xFFFFFFF0u);
                    }
                    return (u64)(bar0 & 0xFFFFFFF0u);
                }
            }
        }
    }
    return 0;
}

/* ── BGA (Bochs/QEMU VBE) ────────────────────────────────────────────────── */
#define BGA_IDX 0x01CE
#define BGA_DAT 0x01CF

static void bga_write(u16 idx, u16 val) {
    outw(BGA_IDX, idx);
    outw(BGA_DAT, val);
}

static void bga_set_mode(u32 w, u32 h, u32 bpp) {
    bga_write(0, 0xB0C0);
    bga_write(4, 0);
    bga_write(1, (u16)w);
    bga_write(2, (u16)h);
    bga_write(3, (u16)bpp);
    bga_write(6, (u16)w);
    bga_write(7, (u16)h);
    bga_write(8, 0);
    bga_write(9, 0);
    bga_write(4, 0x41);
}

/* ── arch entry ──────────────────────────────────────────────────────────── */
void arch_x86_64_entry(u32 magic, u32 mb_info_phys) {
    ramfs_init();

    /* 1. IDT — must come before any code that could fault */
    idt64_init();

    /* 2. Fast syscall path (SYSCALL/SYSRET via LSTAR MSR) */
    syscall64_install();

    /* 3. Framebuffer — priority:
     *   1. EFI GOP (UEFI boot)
     *   2. Multiboot2 framebuffer tag  ← most reliable for VirtualBox/GRUB
     *   3. BGA mode-set + PCI BAR scan (QEMU BIOS path)
     *   4. Hard-coded fallback
     */
    u64 fb_addr  = 0;
    u32 fb_w     = 1024;
    u32 fb_h     = 768;
    u32 fb_bpp   = 32;
    u32 fb_pitch = fb_w * 4;

    /* Accept both Multiboot1 (0x2BADB002) and Multiboot2 (0x36d76289) */
#define MB1_MAGIC 0x2BADB002u
#define MB2_MAGIC 0x36d76289u
#define IS_MB_MAGIC(m) ((m) == MB1_MAGIC || (m) == MB2_MAGIC)

    /* ── 3a. VBE Mode Info Block — BIOS-authoritative LFB address ─────
     * GRUB fills mbi->vbe_mode_info with a pointer to the 256-byte
     * VBE Mode Info Block the BIOS populated during INT 10h/AX=4F01h.
     * Reading LfbPhysBase (offset 0x28) gives the exact address the
     * hardware was programmed with — more reliable than framebuffer_addr
     * which GRUB can truncate or misreport on some firmware.
     *
     * VBE Mode Info Block layout (VESA VBE 2.0):
     *   0x10  u16  BytesPerScanLine
     *   0x12  u16  XResolution
     *   0x14  u16  YResolution
     *   0x19  u8   BitsPerPixel
     *   0x28  u32  PhysBasePtr  ← the actual framebuffer physical address
     */
    if (!fb_addr && magic == MB1_MAGIC && mb_info_phys) {
        multiboot_info_t *mbi = (multiboot_info_t *)(usize)mb_info_phys;
        if ((mbi->flags & (1u << 11)) && mbi->vbe_mode_info) {
            u8  *vmi  = (u8  *)(usize)mbi->vbe_mode_info;
            u32  lfb  = *(u32 *)(vmi + 0x28);
            u16  bpsl = *(u16 *)(vmi + 0x10);
            u16  xres = *(u16 *)(vmi + 0x12);
            u16  yres = *(u16 *)(vmi + 0x14);
            u8   bpp  = *(u8  *)(vmi + 0x19);
            if (lfb && xres && yres && bpp) {
                fb_addr  = (u64)lfb;
                fb_w     = xres;
                fb_h     = yres;
                fb_bpp   = bpp;
                fb_pitch = bpsl ? (u32)bpsl : (fb_w * (fb_bpp / 8));
                printk("fb: VBE BIOS LFB %ux%ux%u @ 0x%x pitch=%u\n",
                       fb_w, fb_h, fb_bpp, (u32)fb_addr, fb_pitch);
            }
        }
    }

    /* ── 3b. EFI GOP ───────────────────────────────────────────────── */
    if (!fb_addr) {
        efi_gop_info_t gop;
        if (efi_gop_init(&gop)) {
            fb_addr  = (u64)gop.framebuffer_base;
            fb_w     = gop.width;
            fb_h     = gop.height;
            fb_pitch = gop.pitch;
            fb_bpp   = gop.bpp;
            printk("fb: EFI GOP %ux%ux%u @ 0x%llx\n",
                   fb_w, fb_h, fb_bpp, fb_addr);
        }
    }

    /* ── 3c. Multiboot framebuffer tag (MB1 bit 12 / MB2 tag 8) ───── */
    if (!fb_addr && IS_MB_MAGIC(magic) && mb_info_phys) {
        if (magic == MB1_MAGIC) {
            multiboot_info_t *mbi = (multiboot_info_t *)(usize)mb_info_phys;
            if (mbi->flags & (1u << 12)) {
                fb_addr  = mbi->framebuffer_addr;
                fb_w     = mbi->framebuffer_width;
                fb_h     = mbi->framebuffer_height;
                fb_pitch = mbi->framebuffer_pitch;
                fb_bpp   = mbi->framebuffer_bpp;
                printk("fb: MB1 tag %ux%ux%u @ 0x%llx pitch=%u\n",
                       fb_w, fb_h, fb_bpp, fb_addr, fb_pitch);
            }
        } else {
            /* Multiboot2 tagged list, tag type=8 = framebuffer */
            u8 *p     = (u8 *)(usize)mb_info_phys;
            u32 total = *(u32 *)p;
            u8 *end   = p + total;
            p += 8;
            while (p < end) {
                u32 tag_type = *(u32 *)p;
                u32 tag_size = *(u32 *)(p + 4);
                if (tag_type == 0) break;
                if (tag_type == 8 && tag_size >= 24) {
                    fb_addr  = *(u64 *)(p + 8);
                    fb_pitch = *(u32 *)(p + 16);
                    fb_w     = *(u32 *)(p + 20);
                    fb_h     = *(u32 *)(p + 24);
                    fb_bpp   = *(u8  *)(p + 28) ? *(u8 *)(p+28) : 32;
                    printk("fb: MB2 tag %ux%ux%u @ 0x%llx pitch=%u\n",
                           fb_w, fb_h, fb_bpp, fb_addr, fb_pitch);
                    break;
                }
                p += (tag_size + 7u) & ~7u;
            }
        }
    }

    /* 3c. BGA mode-set + PCI BAR scan (QEMU stdvga / Bochs) */
    if (!fb_addr) {
        bga_set_mode(fb_w, fb_h, fb_bpp);
        fb_addr = find_vga_bar0();
    }

    /* 3d. No hardcoded fallback address — 0xFD000000 crashes QEMU/VMs
     * if the address isn't actually mapped. Run headless if no FB found. */
    if (!fb_addr)
        printk("fb: no framebuffer found — running headless\n");

    /* 4. Set up printk + VESA */
    {
        u32 *fb = (u32 *)(usize)fb_addr;

        /* Clear framebuffer to black.
         * rep stosq writes 8 bytes per count — compute count as
         * total_bytes/8.  Use fb_h*fb_pitch (actual byte size) NOT
         * fb_w*fb_h (pixel count) — mixing these trashes memory past
         * the framebuffer and silently corrupts everything after. */
        u64 total_bytes = (u64)fb_h * fb_pitch;
        u64 qword_count = total_bytes / 8;
        u8 *fb8 = (u8 *)(usize)fb_addr;
        __asm__ volatile(
            "rep stosq"
            : "+D"(fb8), "+c"(qword_count)
            : "a"((u64)0)
            : "memory"
        );
        /* Handle any trailing bytes (if pitch*h not divisible by 8) */
        u64 rem = total_bytes & 7;
        while (rem--) *fb8++ = 0;
        fb = (u32 *)(usize)fb_addr;

        printk_set_fb(fb, fb_w, fb_h, fb_pitch, fb_bpp);

        /* Real-hardware fix: clear VGA Sequencer Screen-Off bit */
        { extern void display_vga_unblank(void); display_vga_unblank(); }

        g_fb_info.addr   = (u8 *)(usize)fb_addr;
        g_fb_info.pitch  = fb_pitch;
        g_fb_info.width  = fb_w;
        g_fb_info.height = fb_h;
        g_fb_info.bpp    = fb_bpp;

        vesa_init((u32)fb_addr, fb_w, fb_h, fb_pitch, fb_bpp);

        printk("Ark x86_64  %ux%ux%u  pitch=%u  fb=0x%llx\n",
               fb_w, fb_h, fb_bpp, fb_pitch, fb_addr);
    }

    /* 5. Load modules — handle both Multiboot1 and Multiboot2 */
    if (IS_MB_MAGIC(magic) && mb_info_phys) {
        if (magic == MB1_MAGIC) {
            multiboot_info_t *mbi = (multiboot_info_t *)(usize)mb_info_phys;
            if ((mbi->flags & 0x08u) && mbi->mods_count > 0) {
                u32 n = modules_load_from_multiboot(mbi);
                printk("initramfs: %u file(s)\n", n);
            }
        } else {
            /* MB2: walk tags looking for type 3 (modules) */
            u8 *p   = (u8 *)(usize)mb_info_phys;
            u32 total = *(u32 *)p;
            u8 *end = p + total;
            p += 8;
            u32 mod_count = 0;
            while (p < end) {
                u32 tag_type = *(u32 *)p;
                u32 tag_size = *(u32 *)(p + 4);
                if (tag_type == 0) break;
                if (tag_type == 3 && tag_size >= 16) {
                    /* MB2 module tag: u32 mod_start, u32 mod_end, string... */
                    u32 mod_start = *(u32 *)(p + 8);
                    u32 mod_end   = *(u32 *)(p + 12);
                    if (mod_start < mod_end) {
                        ramfs_add_file((u8 *)(usize)mod_start,
                                       mod_end - mod_start, "initrd");
                        mod_count++;
                    }
                }
                u32 padded = (tag_size + 7u) & ~7u;
                p += padded;
            }
            if (mod_count)
                printk("initramfs: %u file(s)\n", mod_count);
        }
    }

    kernel_main();
    kernel_panic("kernel_main returned");
}
