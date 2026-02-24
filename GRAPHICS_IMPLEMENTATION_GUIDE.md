# Graphics Architecture - Implementation Guide

This guide provides step-by-step code templates to implement the graphics system described in `GRAPHICS_ARCHITECTURE.md`.

## Part 1: Core Graphics Renderer (`gfx_renderer.c` & `gfx_renderer.h`)

### Header File: `include/ark/gfx_renderer.h`

```c
/**
 * Graphics Renderer - Software-based pixel drawing and compositing
 * 
 * Provides low-level drawing primitives that output to a back buffer.
 * The framebuffer is only updated via gfx_present().
 */

#pragma once

#include "ark/types.h"

/* Color format: ARGB 32-bit */
typedef u32 color_t;

/* Color constants */
#define GFX_COLOR_BLACK      0xFF000000
#define GFX_COLOR_WHITE      0xFFFFFFFF
#define GFX_COLOR_RED        0xFFFF0000
#define GFX_COLOR_GREEN      0xFF00FF00
#define GFX_COLOR_BLUE       0xFF0000FF
#define GFX_COLOR_TRANSPAR   0x00000000

/* Macro to construct ARGB color */
#define GFX_ARGB(a, r, g, b) \
    (((u32)(a) << 24) | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

/* Canvas represents a surface (on-screen or off-screen) */
typedef struct {
    u32 *pixels;           /* Pixel buffer (ARGB 32-bit per pixel) */
    u32 width;             /* Canvas width in pixels */
    u32 height;            /* Canvas height in pixels */
    u32 pitch;             /* Bytes per scanline */
    
    /* Clipping region */
    u32 clip_x, clip_y;    /* Top-left of clip rect */
    u32 clip_w, clip_h;    /* Clip rect dimensions */
} gfx_canvas_t;

/* Graphics context includes canvas and rendering state */
typedef struct {
    gfx_canvas_t canvas;
    color_t fg_color;      /* Foreground color for shapes */
    color_t bg_color;      /* Background color */
    u32 line_width;        /* Stroke width */
} gfx_context_t;

/* Point structure */
typedef struct {
    s32 x, y;
} gfx_point_t;

/* Rectangle structure */
typedef struct {
    u32 x, y, w, h;
} gfx_rect_t;

/* ============ Initialization ============ */

/**
 * Initialize graphics system with framebuffer info.
 * Allocates back buffer and sets up render context.
 */
void gfx_init(const ark_fb_info_t *fb_info);

/**
 * Shutdown graphics system and free resources.
 */
void gfx_shutdown(void);

/**
 * Get the main graphics context (renders to back buffer).
 */
gfx_context_t *gfx_get_context(void);

/* ============ Canvas Management ============ */

/**
 * Create a new off-screen canvas in RAM.
 * Caller must free with gfx_free_canvas().
 */
gfx_canvas_t *gfx_create_canvas(u32 width, u32 height);

/**
 * Free a canvas created with gfx_create_canvas().
 */
void gfx_free_canvas(gfx_canvas_t *canvas);

/**
 * Clear entire canvas to a single color.
 */
void gfx_clear_canvas(gfx_canvas_t *canvas, color_t color);

/**
 * Set clipping rectangle. All draws after this are clipped.
 */
void gfx_set_clip_rect(gfx_context_t *ctx,
                       u32 x, u32 y, u32 w, u32 h);

/**
 * Reset clipping to canvas bounds.
 */
void gfx_reset_clip(gfx_context_t *ctx);

/* ============ Primitive Drawing ============ */

/**
 * Draw a single pixel at (x, y).
 */
void gfx_draw_pixel(gfx_context_t *ctx, u32 x, u32 y, color_t col);

/**
 * Draw a line from (x0, y0) to (x1, y1) using Bresenham.
 */
void gfx_draw_line(gfx_context_t *ctx, s32 x0, s32 y0,
                   s32 x1, s32 y1, color_t col);

/**
 * Draw hollow rectangle.
 */
void gfx_draw_rect(gfx_context_t *ctx, u32 x, u32 y,
                   u32 w, u32 h, color_t col);

/**
 * Fill solid rectangle (optimized).
 */
void gfx_fill_rect(gfx_context_t *ctx, u32 x, u32 y,
                   u32 w, u32 h, color_t col);

/**
 * Draw hollow circle using midpoint algorithm.
 */
void gfx_draw_circle(gfx_context_t *ctx, s32 cx, s32 cy,
                     u32 radius, color_t col);

/**
 * Fill circle.
 */
void gfx_fill_circle(gfx_context_t *ctx, s32 cx, s32 cy,
                     u32 radius, color_t col);

/* ============ Compositing ============ */

/**
 * Copy pixels from source to destination (no blending).
 */
void gfx_blit_buffer(gfx_context_t *dst,
                     u32 dst_x, u32 dst_y,
                     const gfx_canvas_t *src,
                     u32 src_x, u32 src_y,
                     u32 w, u32 h);

/**
 * Copy with per-pixel alpha blending.
 * global_alpha: 0=transparent, 255=opaque
 */
void gfx_blit_alpha(gfx_context_t *dst,
                    u32 dst_x, u32 dst_y,
                    const gfx_canvas_t *src,
                    u32 src_x, u32 src_y,
                    u32 w, u32 h,
                    u8 global_alpha);

/**
 * Copy with chroma key (transparent color).
 */
void gfx_blit_transparent(gfx_context_t *dst,
                          u32 dst_x, u32 dst_y,
                          const gfx_canvas_t *src,
                          u32 src_x, u32 src_y,
                          u32 w, u32 h,
                          color_t transparent_color);

/* ============ Color Operations ============ */

/**
 * Blend two colors with alpha: result = fg*alpha + bg*(1-alpha)
 */
color_t gfx_alpha_blend(color_t fg, color_t bg, u8 alpha);

/**
 * Convert RGB to ARGB (with full opacity).
 */
color_t gfx_rgb_to_argb(u8 r, u8 g, u8 b);

/**
 * Extract ARGB components from a color.
 */
void gfx_unpack_argb(color_t argb, u8 *a, u8 *r, u8 *g, u8 *b);

/* ============ Display ============ */

/**
 * Flush back buffer to hardware framebuffer.
 * This makes all pending drawing operations visible on screen.
 */
void gfx_present(void);

/**
 * Clear back buffer without flushing.
 */
void gfx_clear(color_t color);

#endif /* GRAPHICS_RENDERER_H */
```

### Implementation: `gen/gfx_renderer.c`

```c
/**
 * Software-based graphics renderer implementation
 */

#include "ark/types.h"
#include "ark/fb.h"
#include "ark/gfx_renderer.h"
#include <string.h>  /* memcpy, memset */

/* Global graphics system state */
typedef struct {
    gfx_canvas_t back_buffer;      /* Off-screen render target (RAM) */
    gfx_context_t main_context;    /* Primary render context */
    ark_fb_info_t fb_info;         /* Hardware framebuffer info */
} graphics_system_t;

static graphics_system_t g_gfx = {0};
static u8 g_gfx_initialized = 0;

/* ============ Initialization ============ */

void gfx_init(const ark_fb_info_t *fb_info) {
    if (!fb_info || !fb_info->addr) {
        return;
    }

    g_gfx.fb_info = *fb_info;

    /* Allocate back buffer (same size as framebuffer) */
    u32 fb_size = fb_info->height * fb_info->pitch;
    g_gfx.back_buffer.pixels = malloc(fb_size);
    
    if (!g_gfx.back_buffer.pixels) {
        printk("[gfx] Error: failed to allocate back buffer\n");
        return;
    }

    g_gfx.back_buffer.width = fb_info->width;
    g_gfx.back_buffer.height = fb_info->height;
    g_gfx.back_buffer.pitch = fb_info->pitch;

    /* Initialize main render context */
    g_gfx.main_context.canvas = g_gfx.back_buffer;
    g_gfx.main_context.fg_color = GFX_COLOR_WHITE;
    g_gfx.main_context.bg_color = GFX_COLOR_BLACK;
    g_gfx.main_context.line_width = 1;

    /* Set initial clipping to full canvas */
    gfx_reset_clip(&g_gfx.main_context);

    /* Clear to black */
    gfx_clear(GFX_COLOR_BLACK);

    g_gfx_initialized = 1;
    printk("[gfx] Graphics system initialized: %ux%u\n",
           fb_info->width, fb_info->height);
}

void gfx_shutdown(void) {
    if (g_gfx.back_buffer.pixels) {
        free(g_gfx.back_buffer.pixels);
        g_gfx.back_buffer.pixels = NULL;
    }
    g_gfx_initialized = 0;
}

gfx_context_t *gfx_get_context(void) {
    return &g_gfx.main_context;
}

/* ============ Canvas Management ============ */

gfx_canvas_t *gfx_create_canvas(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        return NULL;
    }

    gfx_canvas_t *canvas = malloc(sizeof(gfx_canvas_t));
    if (!canvas) {
        return NULL;
    }

    /* Allocate pixel buffer (ARGB, 4 bytes per pixel) */
    u32 size = height * width * 4;
    canvas->pixels = malloc(size);
    
    if (!canvas->pixels) {
        free(canvas);
        return NULL;
    }

    canvas->width = width;
    canvas->height = height;
    canvas->pitch = width * 4;
    canvas->clip_x = 0;
    canvas->clip_y = 0;
    canvas->clip_w = width;
    canvas->clip_h = height;

    return canvas;
}

void gfx_free_canvas(gfx_canvas_t *canvas) {
    if (!canvas) {
        return;
    }
    if (canvas->pixels) {
        free(canvas->pixels);
    }
    free(canvas);
}

void gfx_clear_canvas(gfx_canvas_t *canvas, color_t color) {
    if (!canvas || !canvas->pixels) {
        return;
    }

    u32 *pixels = canvas->pixels;
    u32 count = (canvas->pitch / 4) * canvas->height;

    for (u32 i = 0; i < count; i++) {
        pixels[i] = color;
    }
}

void gfx_set_clip_rect(gfx_context_t *ctx,
                       u32 x, u32 y, u32 w, u32 h) {
    if (!ctx) {
        return;
    }

    /* Clamp to canvas bounds */
    if (x >= ctx->canvas.width) x = ctx->canvas.width;
    if (y >= ctx->canvas.height) y = ctx->canvas.height;
    if (x + w > ctx->canvas.width) w = ctx->canvas.width - x;
    if (y + h > ctx->canvas.height) h = ctx->canvas.height - y;

    ctx->canvas.clip_x = x;
    ctx->canvas.clip_y = y;
    ctx->canvas.clip_w = w;
    ctx->canvas.clip_h = h;
}

void gfx_reset_clip(gfx_context_t *ctx) {
    if (!ctx) {
        return;
    }
    ctx->canvas.clip_x = 0;
    ctx->canvas.clip_y = 0;
    ctx->canvas.clip_w = ctx->canvas.width;
    ctx->canvas.clip_h = ctx->canvas.height;
}

/* ============ Pixel Writing (Core) ============ */

/**
 * Internal: write pixel with bounds and clipping checks
 */
static inline void write_pixel(gfx_canvas_t *canvas, u32 x, u32 y, color_t col) {
    if (!canvas || !canvas->pixels) {
        return;
    }

    /* Check clipping */
    if (x < canvas->clip_x || x >= (canvas->clip_x + canvas->clip_w) ||
        y < canvas->clip_y || y >= (canvas->clip_y + canvas->clip_h)) {
        return;
    }

    /* Check bounds */
    if (x >= canvas->width || y >= canvas->height) {
        return;
    }

    /* Calculate offset and write */
    u32 *row = (u32 *)((u8 *)canvas->pixels + y * canvas->pitch);
    row[x] = col;
}

/* ============ Primitive Drawing ============ */

void gfx_draw_pixel(gfx_context_t *ctx, u32 x, u32 y, color_t col) {
    if (!ctx) {
        return;
    }
    write_pixel(&ctx->canvas, x, y, col);
}

/**
 * Bresenham line algorithm
 */
void gfx_draw_line(gfx_context_t *ctx, s32 x0, s32 y0,
                   s32 x1, s32 y1, color_t col) {
    if (!ctx) {
        return;
    }

    s32 dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    s32 dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    s32 sx = (x0 < x1) ? 1 : -1;
    s32 sy = (y0 < y1) ? 1 : -1;
    s32 err = dx - dy;

    while (1) {
        write_pixel(&ctx->canvas, x0, y0, col);

        if (x0 == x1 && y0 == y1) break;

        s32 e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_draw_rect(gfx_context_t *ctx, u32 x, u32 y,
                   u32 w, u32 h, color_t col) {
    if (!ctx || w == 0 || h == 0) {
        return;
    }

    /* Top and bottom edges */
    for (u32 i = 0; i < w; i++) {
        write_pixel(&ctx->canvas, x + i, y, col);
        write_pixel(&ctx->canvas, x + i, y + h - 1, col);
    }

    /* Left and right edges */
    for (u32 i = 1; i < h - 1; i++) {
        write_pixel(&ctx->canvas, x, y + i, col);
        write_pixel(&ctx->canvas, x + w - 1, y + i, col);
    }
}

void gfx_fill_rect(gfx_context_t *ctx, u32 x, u32 y,
                   u32 w, u32 h, color_t col) {
    if (!ctx || w == 0 || h == 0) {
        return;
    }

    /* Clamp to clipping rectangle */
    u32 clip_x = ctx->canvas.clip_x;
    u32 clip_y = ctx->canvas.clip_y;
    u32 clip_w = ctx->canvas.clip_w;
    u32 clip_h = ctx->canvas.clip_h;

    if (x >= clip_x + clip_w || y >= clip_y + clip_h ||
        x + w <= clip_x || y + h <= clip_y) {
        return;  /* Entirely outside clip region */
    }

    /* Clamp to clip region */
    u32 start_x = (x < clip_x) ? clip_x : x;
    u32 start_y = (y < clip_y) ? clip_y : y;
    u32 end_x = (x + w > clip_x + clip_w) ? clip_x + clip_w : x + w;
    u32 end_y = (y + h > clip_y + clip_h) ? clip_y + clip_h : y + h;

    /* Fast fill using direct memory writes */
    for (u32 row_y = start_y; row_y < end_y; row_y++) {
        u32 *row = (u32 *)((u8 *)ctx->canvas.pixels + row_y * ctx->canvas.pitch);
        for (u32 col_x = start_x; col_x < end_x; col_x++) {
            row[col_x] = col;
        }
    }
}

/**
 * Midpoint circle algorithm
 */
void gfx_draw_circle(gfx_context_t *ctx, s32 cx, s32 cy,
                     u32 radius, color_t col) {
    if (!ctx || radius == 0) {
        return;
    }

    s32 x = radius;
    s32 y = 0;
    s32 d = 3 - 2 * radius;

    while (x >= y) {
        /* Plot 8 symmetric points */
        write_pixel(&ctx->canvas, cx + x, cy + y, col);
        write_pixel(&ctx->canvas, cx + x, cy - y, col);
        write_pixel(&ctx->canvas, cx - x, cy + y, col);
        write_pixel(&ctx->canvas, cx - x, cy - y, col);
        write_pixel(&ctx->canvas, cx + y, cy + x, col);
        write_pixel(&ctx->canvas, cx + y, cy - x, col);
        write_pixel(&ctx->canvas, cx - y, cy + x, col);
        write_pixel(&ctx->canvas, cx - y, cy - x, col);

        if (d < 0) {
            d = d + 4 * y + 6;
        } else {
            d = d + 4 * (y - x) + 10;
            x--;
        }
        y++;
    }
}

void gfx_fill_circle(gfx_context_t *ctx, s32 cx, s32 cy,
                     u32 radius, color_t col) {
    if (!ctx || radius == 0) {
        return;
    }

    for (s32 y = -radius; y <= radius; y++) {
        s32 x_max = (s32)sqrt((double)radius * radius - y * y);
        for (s32 x = -x_max; x <= x_max; x++) {
            write_pixel(&ctx->canvas, cx + x, cy + y, col);
        }
    }
}

/* ============ Compositing ============ */

void gfx_blit_buffer(gfx_context_t *dst,
                     u32 dst_x, u32 dst_y,
                     const gfx_canvas_t *src,
                     u32 src_x, u32 src_y,
                     u32 w, u32 h) {
    if (!dst || !src || !src->pixels) {
        return;
    }

    /* Clamp source to source bounds */
    if (src_x + w > src->width) w = src->width - src_x;
    if (src_y + h > src->height) h = src->height - src_y;

    /* Blit each row */
    for (u32 y = 0; y < h; y++) {
        u32 *src_row = (u32 *)((u8 *)src->pixels + (src_y + y) * src->pitch);
        u32 src_offset = src_x;

        for (u32 x = 0; x < w; x++) {
            color_t pixel = src_row[src_offset + x];
            write_pixel(&dst->canvas, dst_x + x, dst_y + y, pixel);
        }
    }
}

void gfx_blit_alpha(gfx_context_t *dst,
                    u32 dst_x, u32 dst_y,
                    const gfx_canvas_t *src,
                    u32 src_x, u32 src_y,
                    u32 w, u32 h,
                    u8 global_alpha) {
    if (!dst || !src || !src->pixels) {
        return;
    }

    /* Clamp source */
    if (src_x + w > src->width) w = src->width - src_x;
    if (src_y + h > src->height) h = src->height - src_y;

    for (u32 y = 0; y < h; y++) {
        u32 *src_row = (u32 *)((u8 *)src->pixels + (src_y + y) * src->pitch);
        u32 dst_row_y = dst_y + y;

        if (dst_row_y >= dst->canvas.height) break;

        u32 *dst_row = (u32 *)((u8 *)dst->canvas.pixels + 
                               dst_row_y * dst->canvas.pitch);

        for (u32 x = 0; x < w; x++) {
            u32 dst_x_actual = dst_x + x;
            if (dst_x_actual >= dst->canvas.width) break;

            color_t src_pixel = src_row[src_x + x];
            color_t dst_pixel = dst_row[dst_x_actual];

            color_t blended = gfx_alpha_blend(src_pixel, dst_pixel, global_alpha);
            dst_row[dst_x_actual] = blended;
        }
    }
}

void gfx_blit_transparent(gfx_context_t *dst,
                          u32 dst_x, u32 dst_y,
                          const gfx_canvas_t *src,
                          u32 src_x, u32 src_y,
                          u32 w, u32 h,
                          color_t transparent_color) {
    if (!dst || !src || !src->pixels) {
        return;
    }

    if (src_x + w > src->width) w = src->width - src_x;
    if (src_y + h > src->height) h = src->height - src_y;

    for (u32 y = 0; y < h; y++) {
        u32 *src_row = (u32 *)((u8 *)src->pixels + (src_y + y) * src->pitch);

        for (u32 x = 0; x < w; x++) {
            color_t pixel = src_row[src_x + x];
            
            /* Skip transparent pixels */
            if (pixel != transparent_color) {
                write_pixel(&dst->canvas, dst_x + x, dst_y + y, pixel);
            }
        }
    }
}

/* ============ Color Operations ============ */

color_t gfx_alpha_blend(color_t fg, color_t bg, u8 alpha) {
    u8 fa = (fg >> 24) & 0xFF;
    u8 fr = (fg >> 16) & 0xFF;
    u8 fg_g = (fg >> 8) & 0xFF;
    u8 fb = (fg >> 0) & 0xFF;

    u8 ba = (bg >> 24) & 0xFF;
    u8 br = (bg >> 16) & 0xFF;
    u8 bg_g = (bg >> 8) & 0xFF;
    u8 bb = (bg >> 0) & 0xFF;

    u32 a = ((fa * alpha) + (ba * (255 - alpha))) / 255;
    u32 r = ((fr * alpha) + (br * (255 - alpha))) / 255;
    u32 g = ((fg_g * alpha) + (bg_g * (255 - alpha))) / 255;
    u32 b = ((fb * alpha) + (bb * (255 - alpha))) / 255;

    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) |
           ((g & 0xFF) << 8) | (b & 0xFF);
}

color_t gfx_rgb_to_argb(u8 r, u8 g, u8 b) {
    return (0xFF << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

void gfx_unpack_argb(color_t argb, u8 *a, u8 *r, u8 *g, u8 *b) {
    if (a) *a = (argb >> 24) & 0xFF;
    if (r) *r = (argb >> 16) & 0xFF;
    if (g) *g = (argb >> 8) & 0xFF;
    if (b) *b = (argb >> 0) & 0xFF;
}

/* ============ Display ============ */

void gfx_present(void) {
    if (!g_gfx_initialized || !g_gfx.back_buffer.pixels) {
        return;
    }

    /* Copy entire back buffer to hardware framebuffer */
    u32 size = g_gfx.fb_info.height * g_gfx.fb_info.pitch;
    memcpy(g_gfx.fb_info.addr, g_gfx.back_buffer.pixels, size);
}

void gfx_clear(color_t color) {
    if (!g_gfx_initialized) {
        return;
    }
    gfx_clear_canvas(&g_gfx.back_buffer, color);
}
```

---

## Part 2: Text Rendering System

### Header File: `include/ark/text_renderer.h`

```c
/**
 * Text Rendering Layer
 *
 * Renders text using bitmap fonts to a graphics context.
 * Handles cursor management, newlines, and color.
 */

#pragma once

#include "ark/gfx_renderer.h"
#include "ark/types.h"

/* Single character glyph */
typedef struct {
    u8 width;              /* Glyph width in pixels */
    u8 height;             /* Glyph height in pixels */
    s16 bearing_x;         /* X offset from cursor position */
    s16 bearing_y;         /* Y offset from baseline */
    u16 advance_x;         /* Distance to next glyph */
    const u8 *bitmap;      /* Bitmap data (1 bit per pixel, MSB first) */
} text_glyph_t;

/* Font definition */
typedef struct {
    const char *name;      /* Font name */
    u32 line_height;       /* Space between lines */
    u32 char_height;       /* Character cell height */
    const text_glyph_t glyphs[256];  /* ASCII 0-255 */
} text_font_t;

/* Text rendering context */
typedef struct {
    gfx_context_t *gfx_ctx;  /* Graphics context to render to */
    const text_font_t *font; /* Current font */
    
    /* Cursor position in pixels */
    u32 cursor_x, cursor_y;
    
    /* Text region boundaries in pixels */
    u32 margin_left, margin_top;
    u32 margin_right, margin_bottom;
    
    /* Colors */
    color_t text_color;   /* ARGB color for text */
    color_t bg_color;     /* Background color */
    
    /* Behavior flags */
    u8 auto_wrap;         /* Wrap at right margin */
    u8 auto_scroll;       /* Scroll when bottom reached */
} text_context_t;

/* ============ Initialization ============ */

/**
 * Initialize text context with default font.
 */
void text_init(text_context_t *ctx,
               gfx_context_t *gfx_ctx,
               const text_font_t *font,
               u32 margin_left, u32 margin_top,
               u32 margin_right, u32 margin_bottom);

/**
 * Set text rendering color.
 */
void text_set_color(text_context_t *ctx, color_t color);

/**
 * Set cursor position in pixels.
 */
void text_set_cursor(text_context_t *ctx, u32 x, u32 y);

/**
 * Get current cursor position.
 */
void text_get_cursor(text_context_t *ctx, u32 *x, u32 *y);

/* ============ Text Output ============ */

/**
 * Output single character with text wrapping and scrolling.
 */
void text_putc(text_context_t *ctx, char c);

/**
 * Output null-terminated string.
 */
void text_puts(text_context_t *ctx, const char *str);

/**
 * Clear text region.
 */
void text_clear(text_context_t *ctx);

/**
 * Scroll text region up by one line.
 */
void text_scroll(text_context_t *ctx);

/* ============ Built-in Fonts ============ */

/**
 * Default 8x8 bitmap font
 */
extern const text_font_t text_font_default;

#endif /* TEXT_RENDERER_H */
```

### Implementation: `gen/text_renderer.c` (Basic Version)

```c
/**
 * Text Rendering Implementation
 */

#include "ark/text_renderer.h"
#include "ark/printk.h"
#include <string.h>
#include <stdlib.h>

/* Forward declaration for font data (defined at end of file) */
extern const text_glyph_t g_glyphs_8x8[256];

const text_font_t text_font_default = {
    .name = "System 8x8",
    .line_height = 10,
    .char_height = 8,
    .glyphs = { /* Will be initialized with array below */ }
};

/* ============ Initialization ============ */

void text_init(text_context_t *ctx,
               gfx_context_t *gfx_ctx,
               const text_font_t *font,
               u32 margin_left, u32 margin_top,
               u32 margin_right, u32 margin_bottom) {
    if (!ctx || !gfx_ctx) {
        return;
    }

    ctx->gfx_ctx = gfx_ctx;
    ctx->font = font ? font : &text_font_default;
    ctx->cursor_x = margin_left;
    ctx->cursor_y = margin_top;
    ctx->margin_left = margin_left;
    ctx->margin_top = margin_top;
    ctx->margin_right = margin_right;
    ctx->margin_bottom = margin_bottom;
    ctx->text_color = GFX_COLOR_WHITE;
    ctx->bg_color = GFX_COLOR_BLACK;
    ctx->auto_wrap = 1;
    ctx->auto_scroll = 1;
}

void text_set_color(text_context_t *ctx, color_t color) {
    if (ctx) {
        ctx->text_color = color;
    }
}

void text_set_cursor(text_context_t *ctx, u32 x, u32 y) {
    if (ctx) {
        ctx->cursor_x = x;
        ctx->cursor_y = y;
    }
}

void text_get_cursor(text_context_t *ctx, u32 *x, u32 *y) {
    if (ctx && x && y) {
        *x = ctx->cursor_x;
        *y = ctx->cursor_y;
    }
}

/* ============ Internal: Glyph Rendering ============ */

/**
 * Render a single glyph bitmap to graphics context
 */
static void text_render_glyph(text_context_t *ctx,
                              const text_glyph_t *glyph,
                              u32 x, u32 y) {
    if (!ctx || !glyph || !glyph->bitmap) {
        return;
    }

    const u8 *bitmap = glyph->bitmap;
    u32 width = glyph->width;
    u32 height = glyph->height;

    /* Draw glyph as a grid of pixels */
    for (u32 row = 0; row < height; row++) {
        u8 row_bits = bitmap[row];
        
        for (u32 col = 0; col < width; col++) {
            /* Check bit from MSB to LSB */
            u8 bit = (row_bits >> (7 - col)) & 1;
            
            if (bit) {
                /* Set pixel to text color */
                gfx_draw_pixel(ctx->gfx_ctx,
                               x + col, y + row,
                               ctx->text_color);
            } else {
                /* Set pixel to background color */
                gfx_draw_pixel(ctx->gfx_ctx,
                               x + col, y + row,
                               ctx->bg_color);
            }
        }
    }
}

/* ============ Text Output ============ */

void text_putc(text_context_t *ctx, char c) {
    if (!ctx || !ctx->font) {
        return;
    }

    switch (c) {
    case '\n':
        /* Newline: move to left margin, next line */
        ctx->cursor_x = ctx->margin_left;
        ctx->cursor_y += ctx->font->line_height;
        break;

    case '\r':
        /* Carriage return: move to left margin */
        ctx->cursor_x = ctx->margin_left;
        break;

    case '\t':
        /* Tab: advance to next 4-character boundary */
        ctx->cursor_x = (ctx->cursor_x + 32) & ~31U;
        break;

    case '\b':
        /* Backspace: move cursor left and clear */
        if (ctx->cursor_x > ctx->margin_left) {
            ctx->cursor_x -= ctx->font->glyphs[' '].advance_x;
            /* Draw space to erase */
            text_render_glyph(ctx, &ctx->font->glyphs[(u8)' '],
                             ctx->cursor_x, ctx->cursor_y);
        }
        return;

    default:
        /* Regular character */
        {
            const text_glyph_t *glyph = &ctx->font->glyphs[(u8)c];
            
            /* Check if character fits on current line */
            if (ctx->auto_wrap &&
                ctx->cursor_x + glyph->width > ctx->margin_right) {
                /* Wrap to next line */
                ctx->cursor_x = ctx->margin_left;
                ctx->cursor_y += ctx->font->line_height;
            }

            /* Render glyph */
            text_render_glyph(ctx, glyph, ctx->cursor_x, ctx->cursor_y);

            /* Advance cursor */
            ctx->cursor_x += glyph->advance_x;
        }
        break;
    }

    /* Check if cursor went past bottom of region */
    if (ctx->auto_scroll &&
        ctx->cursor_y + ctx->font->char_height > ctx->margin_bottom) {
        text_scroll(ctx);
    }
}

void text_puts(text_context_t *ctx, const char *str) {
    if (!ctx || !str) {
        return;
    }

    while (*str) {
        text_putc(ctx, *str++);
    }
}

void text_clear(text_context_t *ctx) {
    if (!ctx || !ctx->gfx_ctx) {
        return;
    }

    /* Fill text region with background color */
    gfx_fill_rect(ctx->gfx_ctx,
                  ctx->margin_left, ctx->margin_top,
                  ctx->margin_right - ctx->margin_left,
                  ctx->margin_bottom - ctx->margin_top,
                  ctx->bg_color);

    /* Reset cursor */
    ctx->cursor_x = ctx->margin_left;
    ctx->cursor_y = ctx->margin_top;
}

void text_scroll(text_context_t *ctx) {
    if (!ctx || !ctx->gfx_ctx) {
        return;
    }

    u32 scroll_dist = ctx->font->line_height;
    gfx_canvas_t *canvas = &ctx->gfx_ctx->canvas;

    /* Scroll up by moving memory */
    u32 start_y = ctx->margin_top;
    u32 end_y = ctx->margin_bottom - scroll_dist;

    for (u32 y = start_y; y < end_y; y++) {
        u8 *src_row = (u8 *)canvas->pixels + (y + scroll_dist) * canvas->pitch;
        u8 *dst_row = (u8 *)canvas->pixels + y * canvas->pitch;
        
        u32 copy_width = (ctx->margin_right - ctx->margin_left) * 4;
        memcpy(dst_row, src_row, copy_width);
    }

    /* Clear bottom line */
    for (u32 y = end_y; y < ctx->margin_bottom; y++) {
        u32 *row = (u32 *)((u8 *)canvas->pixels + y * canvas->pitch);
        for (u32 x = ctx->margin_left; x < ctx->margin_right; x++) {
            row[x] = ctx->bg_color;
        }
    }

    /* Move cursor up */
    if (ctx->cursor_y > ctx->margin_top) {
        ctx->cursor_y -= scroll_dist;
    }
}

/* ============ Font Data: 8x8 Bitmap Font ============ */

/* Example: just 'A' for demonstration */
static const u8 g_bitmap_A[] = {
    0x18,  /* ...X X... */
    0x24,  /* ..X...X.. */
    0x42,  /* .X.....X. */
    0x42,  /* .X.....X. */
    0x7E,  /* .XXXXXXX. */
    0x81,  /* X.......X */
    0x81,  /* X.......X */
    0x00,  /* ........ */
};

/* For a complete implementation, you would define all 256 glyphs.
   For now, we'll create a simple default glyph for unimplemented characters. */

static const text_glyph_t g_glyphs_8x8[256] = {
    [' '] = { .width = 8, .height = 8, .advance_x = 8, .bitmap = NULL },
    ['A'] = { .width = 8, .height = 8, .advance_x = 8, .bitmap = g_bitmap_A },
    /* ... more characters ... */
};
```

---

## Part 3: Integration with printk

### Modify `gen/printk.c`

Add this to connect `printk` output to graphics:

```c
/* At top of file, after includes */
#include "ark/text_renderer.h"

/* Global text context for kernel messages */
static text_context_t g_kernel_console;
static u8 g_gfx_console_initialized = 0;

/**
 * Initialize graphics console for printk output
 * Called during kernel boot after framebuffer is initialized
 */
void printk_graphics_init(gfx_context_t *gfx_ctx) {
    if (!gfx_ctx) {
        return;
    }

    /* Initialize text rendering with margins */
    text_init(&g_kernel_console,
              gfx_ctx,
              &text_font_default,
              10, 10,                          /* left, top margins */
              gfx_ctx->canvas.width - 10,
              gfx_ctx->canvas.height - 10);   /* right, bottom margins */

    text_set_color(&g_kernel_console, GFX_COLOR_WHITE);
    text_clear(&g_kernel_console);
    
    g_gfx_console_initialized = 1;
    printk("[printk] Graphics console initialized\n");
}

/**
 * Internal: output character to graphics console
 */
static int graphics_putc(char c) {
    if (!g_gfx_console_initialized) {
        return 0;
    }

    text_putc(&g_kernel_console, c);
    return 1;
}

/* Modify main printk function to call graphics_putc */
/* (Existing code) ... in the character output loop:  */
    if (result > 0) {
        if (use_serial) {
            serial_putc(result);
        }
        
        /* NEW: Also output to graphics console if initialized */
        if (g_gfx_console_initialized) {
            graphics_putc(result);
        }
    }
```

---

## Part 4: Quick Integration Checklist

1. **Create header files**:
   - [ ] `include/ark/gfx_renderer.h`
   - [ ] `include/ark/text_renderer.h`
   - [ ] `include/ark/image_loader.h` (Part 4)

2. **Create implementation files**:
   - [ ] `gen/gfx_renderer.c`
   - [ ] `gen/text_renderer.c`
   - [ ] `gen/image_loader.c` (Part 4)

3. **Update build system**:
   - [ ] Add `gfx_renderer.c` to `Makefile`
   - [ ] Add `text_renderer.c` to `Makefile`
   - [ ] Add `image_loader.c` to `Makefile`

4. **Integrate with kernel boot**:
   - [ ] Call `gfx_init(&g_fb_info)` in `kernel_main()`
   - [ ] Call `printk_graphics_init()` to connect printk
   - [ ] Call `gfx_present()` when rendering is complete

5. **Test rendering**:
   - [ ] Draw test pattern (rectangles, circles, text)
   - [ ] Verify printk text appears on screen
   - [ ] Test scrolling and text wrapping

---

## Next: Part 4 (Image Loader)

See the separate implementation guide for BMP loading and image display.

