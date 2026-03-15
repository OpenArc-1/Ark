/**
 * vesa.c - VESA framebuffer driver for Ark
 *
 * vesa_init() accepts the framebuffer address and dimensions reported by
 * the Multiboot bootloader (mbi->framebuffer_*).  No BGA I/O port magic,
 * no hardcoded addresses — the bootloader already set the mode.
 *
 * Run QEMU with: -vga std
 * QEMU will set a linear 32-bpp framebuffer and populate mbi->framebuffer_*.
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "gpu/vesa.h"

static u8   *vesa_fb     = NULL;
static u32   vesa_width  = 0;
static u32   vesa_height = 0;
static u32   vesa_pitch  = 0;
static u32   vesa_bpp    = 0;
static bool  vesa_ready  = false;

void vesa_init(u32 fb_addr, u32 width, u32 height, u32 pitch, u32 bpp) {
    if (vesa_ready || !fb_addr) return;
    vesa_fb     = (u8 *)fb_addr;
    vesa_width  = width;
    vesa_height = height;
    vesa_bpp    = bpp;
    vesa_pitch  = pitch ? pitch : (width * (bpp / 8));
    vesa_ready  = true;
}

void vesa_init_default(void) { /* no-op: use multiboot address */ }

void vesa_putpixel(u32 x, u32 y, u32 color) {
    if (!vesa_ready || x >= vesa_width || y >= vesa_height) return;
    *(u32 *)(vesa_fb + y * vesa_pitch + x * 4) = color;
}

void vesa_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!vesa_ready) return;
    if (x >= vesa_width || y >= vesa_height) return;
    if (x + w > vesa_width)  w = vesa_width  - x;
    if (y + h > vesa_height) h = vesa_height - y;
    for (u32 py = y; py < y + h; py++) {
        u32 *row = (u32 *)(vesa_fb + py * vesa_pitch + x * 4);
        for (u32 px = 0; px < w; px++) row[px] = color;
    }
}

void vesa_clear_screen(u32 color) {
    if (!vesa_ready) return;
    u32 total = vesa_height * (vesa_pitch / 4);
    u32 *p = (u32 *)vesa_fb;
    for (u32 i = 0; i < total; i++) p[i] = color;
}

void vesa_draw_line(u32 x0, u32 y0, u32 x1, u32 y1, u32 color) {
    int dx = x1 > x0 ? (int)(x1-x0) : (int)(x0-x1);
    int dy = y1 > y0 ? (int)(y1-y0) : (int)(y0-y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        vesa_putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void vesa_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg) {
    (void)c; (void)fg;
    vesa_fill_rect(x, y, 8, 16, bg);
}

void vesa_test_pattern(void) {
    if (!vesa_ready) return;
    vesa_clear_screen(0x000000);
    vesa_fill_rect(0,   0,   256, 256, 0x0000FF);
    vesa_fill_rect(768, 0,   256, 256, 0x00FF00);
    vesa_fill_rect(0,   512, 256, 256, 0xFF0000);
    vesa_fill_rect(384, 256, 256, 256, 0xFFFFFF);
    vesa_draw_line(0, 0, vesa_width-1, 0, 0xFFFFFF);
    vesa_draw_line(vesa_width-1, 0, vesa_width-1, vesa_height-1, 0xFFFFFF);
    vesa_draw_line(vesa_width-1, vesa_height-1, 0, vesa_height-1, 0xFFFFFF);
    vesa_draw_line(0, vesa_height-1, 0, 0, 0xFFFFFF);
}

u8   *vesa_get_framebuffer(void) { return vesa_fb; }
u32   vesa_get_width(void)       { return vesa_width; }
u32   vesa_get_height(void)      { return vesa_height; }
u32   vesa_get_pitch(void)       { return vesa_pitch; }
u32   vesa_get_bpp(void)         { return vesa_bpp; }
bool  vesa_is_ready(void)        { return vesa_ready; }
