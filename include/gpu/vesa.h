/**
 * vesa.h - VESA VBE framebuffer driver API
 */

#ifndef VESA_H
#define VESA_H

#include "ark/types.h"

/* ══════════════════════════════════════════════════════════════════════════
 * Initialization
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * vesa_init() - Initialize VESA framebuffer with explicit parameters
 * 
 * @param fb_addr   Physical framebuffer address (from multiboot or BIOS probe)
 * @param width     Horizontal resolution (e.g., 1024)
 * @param height    Vertical resolution (e.g., 768)
 * @param pitch     Bytes per scanline (0 = auto-calculate from width*bpp/8)
 * @param bpp       Bits per pixel (typically 32 for RGB888)
 * 
 * Call this early in kernel_main with values from your bootloader's
 * multiboot info structure.
 */
void vesa_init(u32 fb_addr, u32 width, u32 height, u32 pitch, u32 bpp);

/**
 * vesa_init_default() - Auto-detect framebuffer (QEMU fallback)
 * 
 * Probes common framebuffer addresses used by QEMU/VirtualBox.
 * Not reliable on real hardware — prefer vesa_init() with multiboot values.
 */
void vesa_init_default(void);

/* ══════════════════════════════════════════════════════════════════════════
 * Drawing primitives
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * vesa_putpixel() - Draw a single pixel
 * 
 * @param x      X coordinate (0 = leftmost column)
 * @param y      Y coordinate (0 = topmost row)
 * @param color  RGB color in 0xRRGGBB format (e.g., 0xFF0000 = red)
 */
void vesa_putpixel(u32 x, u32 y, u32 color);

/**
 * vesa_fill_rect() - Fill a rectangle with solid color
 */
void vesa_fill_rect(u32 x, u32 y, u32 width, u32 height, u32 color);

/**
 * vesa_clear_screen() - Fill entire screen with color
 */
void vesa_clear_screen(u32 color);

/**
 * vesa_draw_line() - Draw a line (Bresenham's algorithm)
 */
void vesa_draw_line(u32 x0, u32 y0, u32 x1, u32 y1, u32 color);

/**
 * vesa_draw_char() - Draw an 8x16 character (stub — needs font data)
 */
void vesa_draw_char(u32 x, u32 y, char c, u32 fg_color, u32 bg_color);

/**
 * vesa_test_pattern() - Draw a colorful test pattern for verification
 */
void vesa_test_pattern(void);

/* ══════════════════════════════════════════════════════════════════════════
 * Getters
 * ══════════════════════════════════════════════════════════════════════════ */

u8   *vesa_get_framebuffer(void);  /* Returns linear framebuffer address */
u32   vesa_get_width(void);        /* Returns horizontal resolution */
u32   vesa_get_height(void);       /* Returns vertical resolution */
u32   vesa_get_pitch(void);        /* Returns bytes per scanline */
u32   vesa_get_bpp(void);          /* Returns bits per pixel */
bool  vesa_is_ready(void);         /* Returns true if initialized */

/* ══════════════════════════════════════════════════════════════════════════
 * Color constants (RGB888 format: 0xRRGGBB)
 * ══════════════════════════════════════════════════════════════════════════ */

#define VESA_BLACK      0x000000
#define VESA_WHITE      0xFFFFFF
#define VESA_RED        0xFF0000
#define VESA_GREEN      0x00FF00
#define VESA_BLUE       0x0000FF
#define VESA_CYAN       0x00FFFF
#define VESA_MAGENTA    0xFF00FF
#define VESA_YELLOW     0xFFFF00
#define VESA_ORANGE     0xFF8000
#define VESA_PURPLE     0x8000FF
#define VESA_GRAY       0x808080
#define VESA_DARK_GRAY  0x404040
#define VESA_LIGHT_GRAY 0xC0C0C0

#endif /* VESA_H */