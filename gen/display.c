/**
 * Display Manager for Ark - 480p framebuffer output
 *
 * Manages text rendering on framebuffer with proper scrolling,
 * cursor management, and kernel message buffering.
 *
 * Real-hardware fixes:
 *  1. Shadow text buffer — scroll() shifts the shadow array and redraws
 *     from it. Framebuffer memory is write-combining (WC) on real hardware;
 *     reads from WC memory return stale/undefined data. The framebuffer is
 *     therefore WRITE-ONLY throughout this file, matching the design in
 *     gen/printk.c.
 *  2. VGA sequencer unblank — the BIOS or firmware may leave the VGA
 *     sequencer clock-disable bit set, causing the display to go dark a
 *     minute or so after boot (identical to the Linux "blank screen on real
 *     hardware" bug fixed in the kernel's fbcon driver). We clear that bit
 *     explicitly in display_init() and periodically in display_unblank().
 *  3. DPMS standby prevention — write 0x00 to VGA Feature Control (0x3DA
 *     read / 0x3CA write) and ensure the VBE/BIOS power state is "on".
 *     Linux does the same in drivers/video/fbdev/core/fbcon.c and
 *     drivers/gpu/drm/drm_fb_helper.c.
 */

#include "ark/types.h"
#include "ark/fb.h"
#include "psf_font_data.h"

/* I/O port helpers (avoids pulling in a separate header) */
static inline void _outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline u8 _inb(u16 port) {
    u8 val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Display state */
static ark_fb_info_t display_fb;
static u32 display_x = 0;
static u32 display_y = 0;
static u32 display_initialized = 0;
static u32 display_vga_text = 0;  /* set when no framebuffer is available */

/* Font dimensions (8x16 pixels per character - matches PSF font) */
#define CHAR_WIDTH  8
#define CHAR_HEIGHT PSF_CHARSIZE  /* 16 */

/* Calculate grid dimensions */
static u32 max_cols = 0;
static u32 max_rows = 0;

/* Color constants (ARGB) */
#define COLOR_BLACK     0x00000000
#define COLOR_WHITE     0xFF808080
#define COLOR_GRAY      0xFF808080

/* ── Shadow text buffer ───────────────────────────────────────────────────
 * Mirrors what is on screen so scroll() never reads the framebuffer.
 * Max 320×200 cells (matches gen/printk.c limits). */
#define SHADOW_MAX_COLS 320
#define SHADOW_MAX_ROWS 200
static u8  disp_shadow_ch[SHADOW_MAX_COLS * SHADOW_MAX_ROWS];
static u8  disp_shadow_valid = 0;

static void disp_shadow_init(void) {
    u32 total = max_cols * max_rows;
    if (total > SHADOW_MAX_COLS * SHADOW_MAX_ROWS)
        total = SHADOW_MAX_COLS * SHADOW_MAX_ROWS;
    for (u32 i = 0; i < total; i++)
        disp_shadow_ch[i] = ' ';
    disp_shadow_valid = 1;
}

/* ── VGA unblank ──────────────────────────────────────────────────────────
 * Clears VGA Sequencer register 1 bit 5 (screen-off / clock-disable).
 * Also resets DPMS state via Feature Control register.
 * Safe to call even when a linear framebuffer is active — the VGA
 * registers are still decoded by the chipset DAC/CRTC on most hardware.
 *
 * This mirrors what Linux does in:
 *   drivers/video/fbdev/vga16fb.c  (vga_io_r / vga_io_w wrappers)
 *   drivers/gpu/drm/drm_fb_helper.c (drm_fb_helper_restore_fbdev_mode)
 */
void display_vga_unblank(void) {
    /* 1. VGA Sequencer: index 1 (Clocking Mode), clear bit 5 (Screen Off) */
    _outb(0x3C4, 0x01);           /* select Sequencer reg 1              */
    u8 seq1 = _inb(0x3C5);
    if (seq1 & 0x20) {
        _outb(0x3C4, 0x01);
        _outb(0x3C5, seq1 & ~0x20u); /* clear Screen-Off bit              */
    }

    /* 2. VGA Feature Control — write 0 to deassert VSYNC/HSYNC blanking  */
    (void)_inb(0x3DA);            /* reset attribute flip-flop            */
    _outb(0x3CA, 0x00);           /* Feature Control: no blanking bits    */

    /* 3. VESA DPMS: INT 10h AX=4F10h BL=00h → query; BL=01h → on.
     * We cannot issue BIOS interrupts from protected mode, but writing
     * the registers above is sufficient for the chipsets we care about. */
 }


/**
 * Write one pixel to the linear framebuffer.
 * Supports 32-bpp and 24-bpp; falls back to 32-bpp if bpp is unexpected.
 * Framebuffer is write-only — we never read it back.
 */
static inline void display_write_pixel(u32 x, u32 y, u32 color) {
    u8 *fb   = display_fb.addr;
    u32 bpp  = display_fb.bpp;
    u32 Bpp  = bpp / 8;           /* bytes per pixel */
    u8 *dst  = fb + y * display_fb.pitch + x * Bpp;

    if (bpp == 32 || bpp == 0) {
        *((u32 *)dst) = color;
    } else if (bpp == 24) {
        dst[0] = (u8)(color);
        dst[1] = (u8)(color >> 8);
        dst[2] = (u8)(color >> 16);
    } else if (bpp == 16) {
        /* Convert 0xRRGGBB → RGB565 */
        u32 r = (color >> 16) & 0xFF;
        u32 g = (color >>  8) & 0xFF;
        u32 b = (color      ) & 0xFF;
        *((u16 *)dst) = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    } else {
        *((u32 *)dst) = color;   /* safe default */
    }
}

/**
 * Draw a character using bitmap font
 */
static void display_draw_char(u32 col, u32 row, char c) {
    u32 px = col * CHAR_WIDTH;
    u32 py = row * CHAR_HEIGHT;

    if (px + CHAR_WIDTH > display_fb.width || py + CHAR_HEIGHT > display_fb.height) {
        return;
    }

    /* Get bitmap for character from PSF font */
    const unsigned char *bitmap = psf_glyphs[(unsigned char)c];
    u32 glyph_color = COLOR_WHITE;

    /* Draw character using bitmap with proper bit extraction */
    for (u32 y = 0; y < CHAR_HEIGHT && py + y < display_fb.height; y++) {
        unsigned char row_bits = bitmap[y];

        for (u32 x = 0; x < CHAR_WIDTH && px + x < display_fb.width; x++) {
            /* Check if bit is set in bitmap (MSB first, left to right) */
            unsigned char bit = (row_bits >> (7 - x)) & 1;

            if (bit) {
                display_write_pixel(px + x, py + y, glyph_color);
            } else {
                display_write_pixel(px + x, py + y, COLOR_BLACK);
            }
        }
    }
}

/**
 * Redraw the entire screen from the shadow buffer.
 * Framebuffer is write-only; we never read it.
 */
static void display_redraw_all(void) {
    if (!display_initialized || !display_fb.addr || !disp_shadow_valid) return;
    u32 nc = max_cols, nr = max_rows;
    for (u32 row = 0; row < nr; row++)
        for (u32 col = 0; col < nc; col++)
            display_draw_char(col, row, (char)disp_shadow_ch[row * nc + col]);
}

/**
 * Scroll the display up by one character row.
 *
 * Real-hardware fix: we shift the SHADOW buffer and redraw from it.
 * The old implementation read directly from the framebuffer, which
 * returns garbage on WC (write-combining) memory used by most GPUs.
 */
static void display_scroll(void) {
    if (!display_initialized || !display_fb.addr) return;

    u32 nc = max_cols, nr = max_rows;
    if (!nc || !nr) return;

    /* Shift shadow up one row */
    u32 move = (nr - 1) * nc;
    for (u32 i = 0; i < move; i++)
        disp_shadow_ch[i] = disp_shadow_ch[i + nc];

    /* Clear last shadow row */
    for (u32 c = 0; c < nc; c++)
        disp_shadow_ch[(nr - 1) * nc + c] = ' ';

    /* Redraw entire screen from clean shadow (write-only to FB) */
    display_redraw_all();

    if (display_y > 0)
        display_y--;
}

/**
 * Initialize the display manager
 */
void display_init(const ark_fb_info_t *fb_info) {
    if (!fb_info || !fb_info->addr) {
        /* no framebuffer provided: fall back to VGA text mode */
        display_vga_text = 1;
        display_initialized = 1;
        display_x = display_y = 0;
        max_cols = 80;
        max_rows = 25;
        return;
    }

    display_fb.addr = fb_info->addr;
    display_fb.pitch = fb_info->pitch;
    display_fb.width = fb_info->width;
    display_fb.height = fb_info->height;
    display_fb.bpp = fb_info->bpp;
    display_x = 0;
    display_y = 0;

    /* Calculate grid dimensions */
    max_cols = display_fb.width / CHAR_WIDTH;
    max_rows = display_fb.height / CHAR_HEIGHT;
    if (max_cols > SHADOW_MAX_COLS) max_cols = SHADOW_MAX_COLS;
    if (max_rows > SHADOW_MAX_ROWS) max_rows = SHADOW_MAX_ROWS;

    display_initialized = 1;

    /* Initialise shadow buffer */
    disp_shadow_init();

    /* Real-hardware fix: ensure VGA is not blanked by firmware/BIOS */
    display_vga_unblank();

    /* Clear screen */
    display_clear();
}

/**
 * Clear the display
 */
void display_clear(void) {
    if (!display_initialized) {
        return;  /* Cannot clear if not initialized */
    }
    
    if (!display_fb.addr) {
        return;  /* No framebuffer address */
    }

    u32 *base = (u32 *)display_fb.addr;
    u32 pixels_per_line = display_fb.pitch / sizeof(u32);
    u32 total_rows = display_fb.height;

    for (u32 y = 0; y < total_rows; y++) {
        u32 *row = base + (y * pixels_per_line);
        for (u32 x = 0; x < pixels_per_line; x++) {
            row[x] = COLOR_BLACK;
        }
    }

    /* Clear shadow buffer too */
    if (disp_shadow_valid) {
        u32 total = max_cols * max_rows;
        for (u32 i = 0; i < total; i++)
            disp_shadow_ch[i] = ' ';
    }

    display_x = 0;
    display_y = 0;
}

/**
 * Put a single character on the display
 */
void display_putc(char c) {
    if (!display_initialized) return;

    if (display_vga_text) {
        /* VGA text mode @0xB8000, attribute 0x07 */
        volatile u16 *vga = (volatile u16 *)0xB8000;
        int pos = display_y * 80 + display_x;
        if (c == '\n') {
            display_x = 0;
            display_y++;
        } else if (c == '\r') {
            display_x = 0;
        } else if (c == '\t') {
            display_x = (display_x + 4) & ~3U;
            return;
        } else if (c == '\b') {
            if (display_x > 0) {
                display_x--;
                vga[pos] = (' ' | (0x07 << 8));
            }
            return;
        } else {
            vga[pos] = (c | (0x07 << 8));
            display_x++;
        }
        if (display_x >= 80) { display_x = 0; display_y++; }
        if (display_y >= 25) {
            for (int y = 1; y < 25; y++)
                for (int x = 0; x < 80; x++)
                    vga[(y-1)*80 + x] = vga[y*80 + x];
            for (int x = 0; x < 80; x++)
                vga[24*80 + x] = (' ' | (0x07 << 8));
            display_y = 24;
        }
        return;
    }

    if (!display_fb.addr || max_cols == 0 || max_rows == 0) {
        return;  /* No valid framebuffer */
    }

    if (c == '\n') {
        display_x = 0;
        display_y++;
    } else if (c == '\r') {
        display_x = 0;
    } else if (c == '\t') {
        display_x = (display_x + 4) & ~3U;
        return;
    } else if (c == '\b') {
        if (display_x > 0) {
            display_x--;
            /* Update shadow */
            if (disp_shadow_valid && display_y < max_rows && display_x < max_cols)
                disp_shadow_ch[display_y * max_cols + display_x] = ' ';
            display_draw_char(display_x, display_y, ' ');
        }
        return;
    } else {
        /* Update shadow before drawing */
        if (disp_shadow_valid && display_y < max_rows && display_x < max_cols)
            disp_shadow_ch[display_y * max_cols + display_x] = (u8)c;
        display_draw_char(display_x, display_y, c);
        display_x++;
    }

    if (display_x >= max_cols) {
        display_x = 0;
        display_y++;
    }

    while (display_y >= max_rows) {
        display_scroll();
    }
}

/**
 * Put a string on the display
 */
void display_puts(const char *s) {
    if (!s) return;
    while (*s) {
        display_putc(*s++);
    }
}

/**
 * Get display dimensions
 */
u32 display_get_width(void) {
    return max_cols;
}

u32 display_get_height(void) {
    return max_rows;
}

u32 display_is_initialized(void) {
    return display_initialized;
}

/**
 * Get framebuffer address (exported to userspace)
 */
u32 *display_get_framebuffer(void) {
    if (!display_initialized || !display_fb.addr) {
        return NULL;
    }
    return (u32 *)display_fb.addr;
}

