/**
 * Simple text-mode console on top of a linear framebuffer.
 *
 * This is deliberately minimal and assumes a fixed 8x16 font and
 * a 32-bit ARGB pixel layout.
 */

#include "ark/types.h"
#include "ark/fb.h"

static ark_fb_info_t fb_info;
static u32 cursor_x;
static u32 cursor_y;

/* 8x16 font data would normally live in its own file; here we cheat
 * and render using a very simple coloured block per character cell.
 */

static void fb_draw_cell(u32 x, u32 y, u32 argb) {
    if (!fb_info.addr) {
        return;
    }

    const u32 char_w = 4;  // Reduced from 6 for smaller text
    const u32 char_h = 6;  // Reduced from 8 for smaller text
    u32 px = x * char_w;
    u32 py = y * char_h;

    if (px + char_w > fb_info.width || py + char_h > fb_info.height) {
        return;
    }

    u32 *base = (u32 *)fb_info.addr;

    for (u32 j = 0; j < char_h; ++j) {
        u32 *row = (u32 *)((u8 *)base + (py + j) * fb_info.pitch);
        for (u32 i = 0; i < char_w; ++i) {
            row[px + i] = argb;
        }
    }
}

void fb_init(const ark_fb_info_t *info) {
    fb_info = *info;
    cursor_x = 0;
    cursor_y = 0;
    fb_clear();
}

void fb_clear(void) {
    if (!fb_info.addr) {
        return;
    }

    u32 *base = (u32 *)fb_info.addr;
    u32 pixels_per_line = fb_info.pitch / 4;

    for (u32 y = 0; y < fb_info.height; ++y) {
        u32 *row = (u32 *)((u8 *)base + y * fb_info.pitch);
        for (u32 x = 0; x < pixels_per_line; ++x) {
            row[x] = 0x00000000; /* black */
        }
    }

    cursor_x = 0;
    cursor_y = 0;
}

static void fb_scroll(void) {
    const u32 char_h = 6;  // Updated from 8 to match new font height
    const u32 rows = fb_info.height / char_h;

    if (!fb_info.addr || rows == 0) {
        return;
    }
    u8 *base = fb_info.addr;

    /* Move all lines up by one row of character cells. */
    for (u32 y = 0; y < (rows - 1) * char_h; ++y) {
        u8 *dst = base + y * fb_info.pitch;
        u8 *src = base + (y + char_h) * fb_info.pitch;
        for (u32 i = 0; i < fb_info.pitch; ++i) {
            dst[i] = src[i];
        }
    }

    /* Clear last row. */
    for (u32 y = (rows - 1) * char_h; y < rows * char_h; ++y) {
        u32 *row = (u32 *)(base + y * fb_info.pitch);
        u32 pixels_per_line = fb_info.pitch / 4;
        for (u32 x = 0; x < pixels_per_line; ++x) {
            row[x] = 0x00000000;
        }
    }

    if (cursor_y > 0) {
        cursor_y--;
    }
}

void fb_putc(char c) {
    const u32 char_w = 4;  // Reduced from 6 for smaller text
    const u32 char_h = 6;  // Reduced from 8 for smaller text
    const u32 cols = fb_info.width / char_w;
    const u32 rows = fb_info.height / char_h;

    if (!fb_info.addr || cols == 0 || rows == 0) {
        return;
    }

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3U;
    } else {
        fb_draw_cell(cursor_x, cursor_y, 0xAAAAAAAA); /* light gray instead of white */
        cursor_x++;
    }

    if (cursor_x >= cols) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= rows) {
        fb_scroll();
    }
}

