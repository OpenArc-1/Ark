# Ark Graphics & Text Rendering Architecture

## Executive Summary

This document defines a **layered, software-rendered graphics system** for Ark that treats the display as a pixel grid (like a dot-matrix controller) rather than direct framebuffer manipulation. The system uses **double buffering** to prevent flicker and provides clear abstractions from the hardware framebuffer up through text rendering and image display.

**Key Principle**: The framebuffer is only the *final output device*. Higher-level systems (printk, UI elements, images) never write directly to it.

---

## 1. Graphics Architecture Overview

### 1.1 System Layers (Bottom-to-Top)

```
┌─────────────────────────────────────────────────────────────────┐
│                    KERNEL SYSTEMS                               │
│         (printk, window manager, UI components)                 │
└─────────────────────────────────────────────────────────────────┘
                            ▲
                            │ Uses
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                  TEXT RENDERING SYSTEM                          │
│   (Glyph cache, cursor management, text layout, colors)         │
│                                                                 │
│  - Bitmap Font Manager (early)                                  │
│  - TODO: TTF Rasterizer (future)                                │
└─────────────────────────────────────────────────────────────────┘
                            ▲
                            │ Uses
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                  SOFTWARE RENDERER                              │
│   (Canvas drawing primitives, compositing, effects)             │
│                                                                 │
│  - draw_pixel        - draw_line      - draw_rect              │
│  - fill_rect         - draw_circle    - draw_polygon           │
│  - blit_buffer       - blit_image     - fill_gradient           │
│  - alpha_blend       - color_convert                           │
└─────────────────────────────────────────────────────────────────┘
                            ▲
                            │ Renders to
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                 BACK BUFFER (RAM)                               │
│         (Off-screen canvas in system memory)                    │
│            Dimensions: width × height × 4 bytes                │
│            Format: ARGB 32-bit per pixel                       │
└─────────────────────────────────────────────────────────────────┘
                            ▲
                            │ Syncs to (memcpy on flush)
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│              FRAMEBUFFER DRIVER (fb.c)                          │
│          (Hardware interface, memory mapping)                   │
│                                                                 │
│  - Manages physical/virtual FB address                          │
│  - Pitch and layout information                                │
│  - Double-buffer synchronization                               │
│  - VESA/UEFI GOP interface                                      │
└─────────────────────────────────────────────────────────────────┘
                            ▲
                            │ Writes to
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│              DISPLAY HARDWARE                                   │
│         (Monitor, VRAM, LCD controller)                         │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Key Design Decisions

| Aspect | Decision | Rationale |
|--------|----------|-----------|
| **Pixel Format** | ARGB 32-bit | Simple, supports transparency, no complex conversions |
| **Buffering** | Double buffer | Prevents flicker during complex renders |
| **Text Rendering** | Bitmap fonts (early) | Fast, simple, no dependencies |
| **Image Format** | BMP (converted from user images) | Uncompressed, trivial to decode, no libjpeg/libpng |
| **Rendering** | Pure software | Controls quality, no hardware dependencies, predictable |
| **Coordinate System** | Screen-relative (0,0 = top-left) | Matches typical framebuffer layout |

---

## 2. Rendering Core (Software Renderer)

### 2.1 Core Abstractions

#### Canvas Structure
```c
typedef struct {
    u32 *pixels;           /* Buffer (ARGB 32-bit per pixel) */
    u32 width;
    u32 height;
    u32 pitch;             /* Bytes per line */
    u32 clip_x, clip_y;    /* Clipping region start */
    u32 clip_w, clip_h;    /* Clipping region dimensions */
} graphics_canvas_t;
```

#### Renderer State (Per-Canvas)
```c
typedef struct {
    graphics_canvas_t canvas;
    u32 fg_color;          /* Current foreground color (ARGB) */
    u32 bg_color;          /* Current background color (ARGB) */
    u16 line_width;        /* Stroke width for lines/shapes */
} graphics_context_t;
```

### 2.2 Core Rendering Functions

#### Primitive Functions

```c
/* Write a single pixel with bounds checking */
void gfx_draw_pixel(graphics_context_t *ctx, u32 x, u32 y, u32 argb);

/* Efficient horizontal/vertical line drawing (Bresenham) */
void gfx_draw_line(graphics_context_t *ctx, u32 x0, u32 y0, 
                   u32 x1, u32 y1, u32 argb);

/* Draw a hollow rectangle */
void gfx_draw_rect(graphics_context_t *ctx, u32 x, u32 y, 
                   u32 w, u32 h, u32 argb);

/* Fill a solid rectangle (fast path) */
void gfx_fill_rect(graphics_context_t *ctx, u32 x, u32 y, 
                   u32 w, u32 h, u32 argb);

/* Draw a circle using midpoint algorithm */
void gfx_draw_circle(graphics_context_t *ctx, u32 cx, u32 cy, 
                     u32 r, u32 argb);

/* Fill a circle */
void gfx_fill_circle(graphics_context_t *ctx, u32 cx, u32 cy, 
                     u32 r, u32 argb);

/* Draw polygon from vertex array */
void gfx_draw_polygon(graphics_context_t *ctx, const gfx_point_t *verts,
                      u32 vert_count, u32 argb);

/* Fill polygon using scanline algorithm */
void gfx_fill_polygon(graphics_context_t *ctx, const gfx_point_t *verts,
                      u32 vert_count, u32 argb);
```

#### Compositing Functions

```c
/* Copy pixel data from one buffer to another (full or partial) */
void gfx_blit_buffer(graphics_context_t *dst,
                     u32 dst_x, u32 dst_y,
                     const graphics_canvas_t *src,
                     u32 src_x, u32 src_y,
                     u32 w, u32 h);

/* Copy with per-pixel alpha blending */
void gfx_blit_alpha(graphics_context_t *dst,
                    u32 dst_x, u32 dst_y,
                    const graphics_canvas_t *src,
                    u32 src_x, u32 src_y,
                    u32 w, u32 h,
                    u8 global_alpha);  /* 0-255 */

/* Copy with chroma key (transparent color) */
void gfx_blit_transparent(graphics_context_t *dst,
                          u32 dst_x, u32 dst_y,
                          const graphics_canvas_t *src,
                          u32 src_x, u32 src_y,
                          u32 w, u32 h,
                          u32 transparent_argb);

/* Draw an image file (BMP) at position */
void gfx_draw_bmp_image(graphics_context_t *ctx,
                        u32 x, u32 y,
                        const char *bmp_filename);
```

#### Color & Effects Functions

```c
/* Blend two colors with alpha */
u32 gfx_alpha_blend(u32 fg_argb, u32 bg_argb, u8 alpha);

/* Convert RGB888 to ARGB with opaque alpha */
u32 gfx_rgb_to_argb(u8 r, u8 g, u8 b);

/* Extract components from ARGB */
void gfx_argb_unpack(u32 argb, u8 *a, u8 *r, u8 *g, u8 *b);

/* Fill gradient (horizontal/vertical) */
void gfx_fill_gradient_h(graphics_context_t *ctx,
                         u32 x, u32 y, u32 w, u32 h,
                         u32 color_left, u32 color_right);

void gfx_fill_gradient_v(graphics_context_t *ctx,
                         u32 x, u32 y, u32 w, u32 h,
                         u32 color_top, u32 color_bottom);
```

#### Buffer Management

```c
/* Create a new off-screen canvas in RAM */
graphics_canvas_t *gfx_create_canvas(u32 width, u32 height);

/* Free a canvas */
void gfx_free_canvas(graphics_canvas_t *canvas);

/* Clear entire canvas to color */
void gfx_clear_canvas(graphics_canvas_t *canvas, u32 color);

/* Set clipping rectangle (all draws are clipped to this region) */
void gfx_set_clip_rect(graphics_context_t *ctx,
                       u32 x, u32 y, u32 w, u32 h);

/* Reset clipping to full canvas */
void gfx_reset_clip(graphics_context_t *ctx);
```

### 2.3 Rendering Pipeline Example: Drawing to Screen

```
User code calls:
    gfx_fill_rect(ctx, 10, 20, 100, 50, 0xFF0000FF)  // red button

    │
    ├─> gfx_fill_rect() validates and clips coordinates
    │
    ├─> For each scanline y in clipped region:
    │       row_offset = y * canvas.pitch
    │       For each pixel x in clipped region:
    │           canvas.pixels[row_offset + x] = color
    │
    └─> Output: pixels written to back_buffer (in RAM)

Later, when rendering is complete:
    gfx_flush_to_framebuffer()
    
    │
    ├─> Calls memcpy(fb_addr, back_buffer, fb_size)
    │       (copies back buffer to hardware framebuffer)
    │
    └─> Monitor displays the pixels (no flicker!)
```

### 2.4 Double Buffering Implementation

```c
/* Global render state */
typedef struct {
    graphics_canvas_t back_buffer;     /* Off-screen canvas (RAM) */
    ark_fb_info_t fb_info;             /* Hardware framebuffer info */
    graphics_context_t render_ctx;     /* Current render context */
} graphics_system_t;

static graphics_system_t g_graphics;

/* Initialize graphics system */
void gfx_init(const ark_fb_info_t *fb_info) {
    g_graphics.fb_info = *fb_info;
    
    /* Allocate back buffer in RAM (same size as framebuffer) */
    g_graphics.back_buffer.pixels = malloc(fb_info->height * fb_info->pitch);
    g_graphics.back_buffer.width = fb_info->width;
    g_graphics.back_buffer.height = fb_info->height;
    g_graphics.back_buffer.pitch = fb_info->pitch;
    
    /* Initialize render context to back buffer */
    g_graphics.render_ctx.canvas = g_graphics.back_buffer;
    g_graphics.render_ctx.fg_color = 0xFFFFFFFF;  /* white */
    g_graphics.render_ctx.bg_color = 0xFF000000;  /* black */
    
    gfx_clear_canvas(&g_graphics.back_buffer, 0xFF000000);
}

/* Flush back buffer to visible framebuffer */
void gfx_present(void) {
    u8 *src = (u8 *)g_graphics.back_buffer.pixels;
    u8 *dst = g_graphics.fb_info.addr;
    u32 size = g_graphics.fb_info.height * g_graphics.fb_info.pitch;
    
    /* Atomic display update (prevents tearing, reduces flicker) */
    memcpy(dst, src, size);
}
```

---

## 3. Text Rendering System

### 3.1 Architecture Overview

```
┌─────────────────────────────────────────┐
│    Application (printk, UI text)        │
└─────────────────────────────────────────┘
                  ▲
                  │ Calls text_render_string()
                  ▼
┌─────────────────────────────────────────┐
│   TEXT RENDERING LAYER                  │
│  - Cursor management                    │
│  - Newline & wrapping handling          │
│  - Color management                     │
│  - Glyph caching (future)               │
└─────────────────────────────────────────┘
                  ▲
                  │ Calls glyph_render()
                  ▼
┌─────────────────────────────────────────┐
│   FONT SYSTEM (PLUGGABLE)              │
│  - Bitmap Font Handler (current)        │
│  - TTF Rasterizer (future)              │
│                                         │
│  Returns per-glyph raster data          │
└─────────────────────────────────────────┘
                  ▲
                  │ Calls gfx_blit_alpha()
                  ▼
┌─────────────────────────────────────────┐
│  GRAPHICS RENDERER                      │
│   (Composites glyph onto back buffer)   │
└─────────────────────────────────────────┘
```

### 3.2 Bitmap Font System (Phase 1)

#### Font Data Structure

```c
/* Single glyph bitmap */
typedef struct {
    u8  width;             /* Glyph width in pixels */
    u8  height;            /* Glyph height in pixels */
    s16 bearing_x;         /* X offset from cursor */
    s16 bearing_y;         /* Y offset from baseline */
    u16 advance_x;         /* Advance width to next glyph */
    const u8 *bitmap;      /* Glyph pixel data (1-bit per pixel) */
} font_glyph_t;

/* Font definition */
typedef struct {
    const char *name;      /* e.g., "Liberation Mono 12" */
    u32 line_height;       /* Space between lines */
    u32 char_height;       /* Height of character cells */
    const font_glyph_t glyphs[256];  /* Per-character glyph data */
} font_t;

/* Built-in font (8x8 bitmap) */
extern const font_t font_default;
extern const font_t font_small;
```

#### Font Data Example (8x8 Bitmap Font)

```c
/* One byte = one pixel row, MSB first (left to right) */
static const u8 glyph_A_bitmap[] = {
    0x18,  /* .  .  X X . . . . */
    0x24,  /* . . X . . X . . */
    0x42,  /* . X . . . . X . */
    0x42,  /* . X . . . . X . */
    0x7E,  /* . X X X X X X . */
    0x81,  /* X . . . . . . X */
    0x81,  /* X . . . . . . X */
    0x00,  /* . . . . . . . . */
};

static const font_glyph_t glyph_'A' = {
    .width = 8,
    .height = 8,
    .bearing_x = 0,
    .bearing_y = 0,
    .advance_x = 8,
    .bitmap = glyph_A_bitmap,
};
```

#### Font Rendering Function

```c
/* Render a glyph bitmap onto canvas */
void font_render_glyph(graphics_context_t *ctx,
                       const font_glyph_t *glyph,
                       u32 x, u32 y,
                       u32 fg_color) {
    const u8 *bitmap = glyph->bitmap;
    u32 glyph_width = glyph->width;
    u32 glyph_height = glyph->height;
    
    for (u32 row = 0; row < glyph_height; row++) {
        u8 row_bits = bitmap[row];
        
        for (u32 col = 0; col < glyph_width; col++) {
            /* Check bit from MSB to LSB */
            u8 bit = (row_bits >> (7 - col)) & 1;
            
            if (bit) {
                gfx_draw_pixel(ctx, x + col, y + row, fg_color);
            }
            /* Unset pixels use canvas background (already filled) */
        }
    }
}
```

### 3.3 Text Rendering Layer

#### Console/Text Context

```c
typedef struct {
    graphics_context_t *gfx_ctx;       /* Rendering context */
    const font_t *font;                 /* Current font */
    u32 cursor_x, cursor_y;             /* Current cursor (in pixels) */
    u32 margin_left, margin_right;      /* Text margins */
    u32 margin_top, margin_bottom;      /* Text margins */
    u32 text_color;                     /* Current text color (ARGB) */
    u32 bg_color;                       /* Background color */
    u8  auto_newline;                   /* Wrap at margin */
    u8  auto_scroll;                    /* Scroll when bottom reached */
} text_renderer_t;
```

#### Text Output Functions

```c
/* Initialize text renderer */
void text_init(text_renderer_t *txt_ctx,
               graphics_context_t *gfx_ctx,
               const font_t *font,
               u32 margin_left, u32 margin_top,
               u32 margin_right, u32 margin_bottom);

/* Output single character with word wrap and scrolling */
void text_putc(text_renderer_t *ctx, char c);

/* Output formatted string */
void text_puts(text_renderer_t *ctx, const char *str);

/* Set text color (ARGB) */
void text_set_color(text_renderer_t *ctx, u32 argb);

/* Set text position */
void text_set_cursor(text_renderer_t *ctx, u32 x, u32 y);

/* Clear text region */
void text_clear(text_renderer_t *ctx);

/* Scroll text region up by one line */
void text_scroll(text_renderer_t *ctx);
```

#### printk Integration

```c
/* Global text rendering context */
static text_renderer_t g_console;

/* Called during kernel initialization */
void console_graphics_init(graphics_context_t *gfx_ctx) {
    text_init(&g_console, gfx_ctx, &font_default,
              10, 10,  /* left, top margins */
              gfx_ctx->canvas.width - 10,
              gfx_ctx->canvas.height - 10);  /* right, bottom margins */
}

/* printk backend for graphics output */
int graphics_putc(char c) {
    text_putc(&g_console, c);
    return 1;
}
```

### 3.4 Future: TTF Font Support (Upgrade Path)

**Goal**: Support TrueType fonts without breaking the current bitmap system.

#### Abstraction: Pluggable Font Backend

```c
typedef struct {
    void (*init)(const char *font_file);
    const font_glyph_t *(*get_glyph)(u32 codepoint, u32 size_pixels);
    void (*free)(void);
} font_backend_t;

typedef struct {
    const char *name;
    font_backend_t backend;
    u32 default_size;
} font_family_t;

/* Backend implementation for bitmap fonts */
const font_glyph_t *bitmap_font_get_glyph(u32 codepoint, u32 size) {
    if (codepoint >= 256) return NULL;
    return &font_default.glyphs[codepoint];
}

const font_backend_t bitmap_backend = {
    .init = NULL,
    .get_glyph = bitmap_font_get_glyph,
    .free = NULL,
};

/* Backend implementation for TTF (future) */
const font_glyph_t *ttf_font_get_glyph(u32 codepoint, u32 size) {
    /* Use freetype or stb_truetype to rasterize glyph */
    static font_glyph_t temp_glyph;
    u8 *bitmap = malloc(size * size * 4);  /* Allocate temp buffer */
    
    /* Rasterize codepoint from .ttf file */
    ttf_rasterize(codepoint, size, bitmap);
    
    /* Fill temp_glyph structure */
    temp_glyph.bitmap = bitmap;
    temp_glyph.width = size;
    temp_glyph.height = size;
    
    return &temp_glyph;
}

const font_backend_t ttf_backend = {
    .init = ttf_init,
    .get_glyph = ttf_font_get_glyph,
    .free = ttf_free,
};

/* Runtime selection */
void use_ttf_font(const char *filepath) {
    ttf_backend.init(filepath);
    g_console.font_backend = &ttf_backend;
}
```

**Key Advantage**: The text rendering layer (`text_putc`, `text_puts`) doesn't change—only the underlying `font_render_glyph()` queries the backend instead of assuming bitmap glyphs.

---

## 4. Image Pipeline

### 4.1 Design Principles

1. **No compression codecs in kernel**: User images (JPEG, PNG) are **converted to BMP offline**
2. **Temporary RAM conversion**: Bitmap is loaded into RAM, converted if needed, rendered, then freed
3. **Original files untouched**: Only the converted copy in RAM is ever deleted
4. **Efficient pipeline**: BMP → pixel buffer → renderer → screen

### 4.2 Image File Structure

#### BMP File Format (Simplified)

```c
typedef struct {
    u16 magic;              /* 'BM' = 0x4D42 */
    u32 file_size;          /* Total file size */
    u16 reserved1, reserved2;
    u32 pixel_offset;       /* Offset to pixel data */
} bmp_header_t;

typedef struct {
    u32 header_size;        /* 40 bytes for DIB header */
    s32 width;
    s32 height;             /* Positive = bottom-up, Negative = top-down */
    u16 planes;             /* Must be 1 */
    u16 bits_per_pixel;     /* 1, 4, 8, 16, 24, or 32 */
    u32 compression;        /* 0 = none, 1 = RLE8, 2 = RLE4 */
    u32 image_size;
    s32 pixels_per_meter_x;
    s32 pixels_per_meter_y;
    u32 colors_used;
    u32 colors_important;
} bmp_dib_header_t;
```

### 4.3 Image Loading Pipeline

```
User code:
    gfx_draw_bmp_image(ctx, 10, 20, "/mnt/vfs/photo.bmp")
                            │
                            ▼
    Open file from VFS
                            │
                            ▼
    Read BMP header + DIB header
                            │
                            ├─ Validate: magic == 'BM', width > 0, height > 0
                            ├─ Allocate temp pixel buffer: width × height × 4 bytes
                            ▼
    Decode pixel data (format conversion):
                            │
                            ├─ If bpp = 32: direct memcpy to temp buffer (already ARGB)
                            ├─ If bpp = 24: convert BGR→ARGB per-pixel
                            ├─ If bpp = 8:  read palette, expand palette indices→ARGB
                            ├─ If bpp = 1:  expand bits to pixels, apply monochrome palette
                            └─ Handle bottom-up vs top-down orientation
                            │
                            ▼
    Create graphics_canvas from decoded pixels
                            │
                            ▼
    gfx_blit_buffer(ctx, 10, 20, &image_canvas, 0, 0, w, h)
                            │ (Composes to back buffer)
                            ▼
    Free temp pixel buffer & close file
                            │
                            ▼
    User calls gfx_present() to display
```

### 4.4 Implementation Functions

```c
/* Load BMP file and return decoded canvas */
graphics_canvas_t *image_load_bmp(const char *filepath);

/* Load BMP by file descriptor (for VFS integration) */
graphics_canvas_t *image_load_bmp_fd(void *file_descriptor);

/* Helper: convert 24-bit BGR → 32-bit ARGB */
static u32 bgr24_to_argb(const u8 *bgr_pixels, unsigned offset) {
    u8 b = bgr_pixels[offset * 3 + 0];
    u8 g = bgr_pixels[offset * 3 + 1];
    u8 r = bgr_pixels[offset * 3 + 2];
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

/* Helper: convert indexed (8-bit) → ARGB using palette */
u32 indexed8_to_argb(u8 index, const u32 *palette) {
    return palette[index];
}

/* Load and display in one call */
void gfx_draw_image(graphics_context_t *ctx, u32 x, u32 y,
                    const char *filename) {
    graphics_canvas_t *img = image_load_bmp(filename);
    if (!img) return;  /* Load failed */
    
    gfx_blit_buffer(ctx, x, y, img, 0, 0, img->width, img->height);
    gfx_free_canvas(img);  /* Free temp buffer immediately */
}
```

### 4.5 Memory Management Example

```c
void gfx_draw_bmp_image(graphics_context_t *ctx, u32 x, u32 y,
                        const char *bmp_filename) {
    /* Step 1: Load file */
    FILE *fp = fs_open(bmp_filename, VFS_READ);
    if (!fp) {
        printk("[gfx-img] Error: cannot open %s\n", bmp_filename);
        return;
    }

    /* Step 2: Parse headers */
    bmp_header_t hdr;
    bmp_dib_header_t dib;
    fs_read(fp, &hdr, sizeof(hdr));
    fs_read(fp, &dib, sizeof(dib));

    if (hdr.magic != 0x4D42) {
        printk("[gfx-img] Error: %s not a BMP file\n", bmp_filename);
        fs_close(fp);
        return;
    }

    /* Step 3: Allocate temporary pixel buffer (in RAM) */
    u32 pixel_count = dib.width * dib.height;
    u32 *pixels = malloc(pixel_count * 4);  /* ARGB */
    
    if (!pixels) {
        printk("[gfx-img] Error: out of memory for%s (%d x %d)\n",
               bmp_filename, dib.width, dib.height);
        fs_close(fp);
        return;
    }

    /* Step 4: Decode pixel data based on format */
    for (u32 i = 0; i < pixel_count; i++) {
        if (dib.bits_per_pixel == 24) {
            pixels[i] = bgr24_to_argb(raw_bmp_data, i);
        } else if (dib.bits_per_pixel == 32) {
            /* Already ARGB, direct copy */
            pixels[i] = raw_bmp_data[i * 4];
        } else {
            /* Unsupported format */
            pixels[i] = 0xFF000000;  /* Black fallback */
        }
    }

    /* Step 5: Create canvas from pixels */
    graphics_canvas_t img = {
        .pixels = pixels,
        .width = dib.width,
        .height = dib.height,
        .pitch = dib.width * 4,
    };

    /* Step 6: Composite to back buffer */
    gfx_blit_buffer(ctx, x, y, &img, 0, 0, dib.width, dib.height);

    /* Step 7: <-- CRITICAL: Free temp memory immediately */
    free(pixels);
    fs_close(fp);
    
    /* ONLY the converted copy in RAM is deleted, never the original .bmp */
}
```

---

## 5. System Philosophy & Design Patterns

### 5.1 Layering Principle

```
RULE: Lower layers are NEVER aware of higher layers
      Higher layers ALWAYS call down the stack only
```

```
✗ WRONG: Text layer directly writes to framebuffer
         → Violates layering, hard to debug, tight coupling

✓ CORRECT: Text layer → Renderer → Back Buffer → Framebuffer Driver
           → Clean separation, reusable components, easy to extend
```

### 5.2 Abstraction Levels

| Level | Responsibility | Never Knows About |
|-------|-----------------|-------------------|
| **1. Framebuffer Driver** | Physical VRAM access, pitch, address | Glyphs, images, pens, colors |
| **2. Graphics Renderer** | Primitives (line, rect, circle) | Text, fonts, applications |
| **3. Text System** | Cursor, newlines, glyph rendering | printk backend, window systems |
| **4. Application** | printk, UI, game logic | Back buffers, pixel formats |

### 5.3 Composition Over Inheritance

```c
/* GOOD: Glyphs composed with graphics context */
typedef struct {
    graphics_context_t *gfx_ctx;  /* Delegates to renderer */
    const font_t *font;
    u32 text_color;
} text_renderer_t;

/* Text calls into graphics as needed */

/* AVOID: Inheritance-like structures */
/* (Not applicable in C, but principle applies to design) */
```

### 5.4 Extension Points (Future Features)

**Windowing System** (when ready):
```c
typedef struct {
    graphics_canvas_t canvas;  /* Private framebuffer for window */
    u32 x, y, w, h;           /* Position on screen */
    /* ... event handling ... */
} window_t;

void window_render(window_t *w) {
    /* Draw window contents to w->canvas (off-screen) */
    gfx_fill_rect(&w->gfx_ctx, ...);
    text_puts(&w->text_ctx, "Hello");
}

void window_composite_all(void) {
    /* Composite all windows to back buffer in Z-order */
    for_each_window(w) {
        gfx_blit_buffer(&g_graphics.render_ctx,
                        w->x, w->y,
                        &w->canvas,
                        0, 0, w->w, w->h);
    }
    gfx_present();  /* Single display refresh */
}
```

**Mouse Cursor** (when ready):
```c
typedef struct {
    graphics_canvas_t shape;   /* Cursor bitmap */
    u32 x, y;                  /* Screen position */
    u32 hotspot_x, hotspot_y;  /* Click origin */
} cursor_t;

void render_frame_with_cursor(void) {
    /* Render scene to back buffer */
    /* ... render windows, text, etc. ... */
    
    /* Composite cursor last (always on top) */
    gfx_blit_alpha(&g_graphics.render_ctx,
                   cursor.x, cursor.y,
                   &cursor.shape,
                   0, 0,
                   cursor.shape.width,
                   cursor.shape.height,
                   255);  /* Fully opaque */
    
    gfx_present();  /* Display frame with cursor */
}
```

### 5.5 Color Management

```c
/* Unified color representation: ARGB 32-bit */
#define COLOR_WHITE        0xFFFFFFFF
#define COLOR_BLACK        0xFF000000
#define COLOR_RED          0xFFFF0000
#define COLOR_GREEN        0xFF00FF00
#define COLOR_BLUE         0xFF0000FF
#define COLOR_TRANSPARENT  0x00000000

/* Macro for easy ARGB construction */
#define ARGB(a, r, g, b)   (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

/* Alpha blending: output = fg * alpha + bg * (1 - alpha) */
u32 gfx_alpha_blend(u32 fg_argb, u32 bg_argb, u8 alpha) {
    u8 fa = (fg_argb >> 24) & 0xFF;
    u8 fr = (fg_argb >> 16) & 0xFF;
    u8 fg = (fg_argb >> 8) & 0xFF;
    u8 fb = (fg_argb >> 0) & 0xFF;

    u8 ba = (bg_argb >> 24) & 0xFF;
    u8 br = (bg_argb >> 16) & 0xFF;
    u8 bg = (bg_argb >> 8) & 0xFF;
    u8 bb = (bg_argb >> 0) & 0xFF;

    u32 a = ((fa * alpha) + (ba * (255 - alpha))) / 255;
    u32 r = ((fr * alpha) + (br * (255 - alpha))) / 255;
    u32 g_ = ((fg * alpha) + (bg * (255 - alpha))) / 255;
    u32 b = ((fb * alpha) + (bb * (255 - alpha))) / 255;

    return (a << 24) | (r << 16) | (g_ << 8) | b;
}
```

---

## 6. Implementation Roadmap

### Phase 1: Foundation (Current/Immediate)

- [x] Framebuffer driver (`fb.c`) — already present
- [x] Display manager (`display.c`) — already present
- [ ] Graphics renderer module (`gfx_renderer.c`)
  - [ ] Core primitives: `gfx_draw_pixel`, `gfx_draw_line`, `gfx_fill_rect`
  - [ ] Buffer operations: `gfx_blit_buffer`, canvas management
  - [ ] Double buffering: back buffer allocation and `gfx_present()`
- [ ] Text rendering layer (`text_renderer.c`)
  - [ ] `text_putc`, `text_puts`, cursor/newline handling
  - [ ] Integration with printk backend
  - [ ] Font structure refactoring

### Phase 2: Enhancement (Next)

- [ ] Extended graphics primitives
  - [ ] Circles, polygons (mid-algorithm)
  - [ ] Gradient fills
  - [ ] Alpha blending
- [ ] Image pipeline (`image_loader.c`)
  - [ ] BMP decoder
  - [ ] `gfx_draw_bmp_image` integration
- [ ] Glyph caching (optional optimization)

### Phase 3: Advanced (Future)

- [ ] Windowing manager (`window.c`)
  - [ ] Window creation, z-order
  - [ ] Composite rendering
- [ ] Mouse/cursor support
- [ ] TTF font backend (replace bitmap with pluggable system)
- [ ] Animations/transitions
- [ ] Hardware acceleration (if target hardware supports it)

---

## 7. Header Files Structure

### `include/ark/graphics.h` (Main API)

```c
#pragma once
#include "ark/types.h"
#include "ark/fb.h"

/* Core types */
typedef struct {
    u32 *pixels;
    u32 width, height, pitch;
    u32 clip_x, clip_y, clip_w, clip_h;
} graphics_canvas_t;

typedef struct {
    graphics_canvas_t canvas;
    u32 fg_color, bg_color;
    u16 line_width;
} graphics_context_t;

/* Initialization */
void gfx_init(const ark_fb_info_t *fb_info);
void gfx_clear_canvas(graphics_canvas_t *canvas, u32 color);
void gfx_present(void);

/* Primitives */
void gfx_draw_pixel(graphics_context_t *ctx, u32 x, u32 y, u32 argb);
void gfx_draw_line(graphics_context_t *ctx, u32 x0, u32 y0, u32 x1, u32 y1, u32 argb);
void gfx_draw_rect(graphics_context_t *ctx, u32 x, u32 y, u32 w, u32 h, u32 argb);
void gfx_fill_rect(graphics_context_t *ctx, u32 x, u32 y, u32 w, u32 h, u32 argb);

/* Compositing */
void gfx_blit_buffer(graphics_context_t *dst, u32 dx, u32 dy,
                     const graphics_canvas_t *src, u32 sx, u32 sy, u32 w, u32 h);

/* Colors */
u32 gfx_rgb_to_argb(u8 r, u8 g, u8 b);
u32 gfx_alpha_blend(u32 fg, u32 bg, u8 alpha);

/* Canvas */
graphics_canvas_t *gfx_create_canvas(u32 w, u32 h);
void gfx_free_canvas(graphics_canvas_t *c);
```

### `include/ark/text.h` (Text Rendering)

```c
#pragma once
#include "ark/graphics.h"

typedef struct {
    const char *name;
    u32 line_height, char_height;
    const font_glyph_t *glyphs_ptr[256];
} font_t;

typedef struct {
    graphics_context_t *gfx_ctx;
    const font_t *font;
    u32 cursor_x, cursor_y;
    u32 text_color, bg_color;
} text_renderer_t;

void text_init(text_renderer_t *ctx, graphics_context_t *gfx,
               const font_t *font, u32 margin_l, u32 margin_t,
               u32 margin_r, u32 margin_b);
void text_putc(text_renderer_t *ctx, char c);
void text_puts(text_renderer_t *ctx, const char *str);
void text_set_color(text_renderer_t *ctx, u32 argb);
void text_clear(text_renderer_t *ctx);
```

### `include/ark/image.h` (Image Loading)

```c
#pragma once
#include "ark/graphics.h"

graphics_canvas_t *image_load_bmp(const char *filepath);
void image_draw_bmp(graphics_context_t *ctx, u32 x, u32 y,
                    const char *bmp_filename);
void image_canvas_free(graphics_canvas_t *canvas);
```

---

## 8. Data Flow Diagrams

### Complete Rendering Flow

```
┌──────────────────────────────────────────────────────────┐
│ Application (printk "Hello, World!" at 100,100)          │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ printk backend: console_graphics_puts("Hello, World!")   │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ text_puts(&g_console, "Hello, World!")                   │
│ For each char 'H', 'e', 'l', ...                         │
│     text_putc(&g_console, char)                          │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ text_putc():                                              │
│   1. Look up 'H' glyph from font_default.glyphs['H']    │
│   2. Call font_render_glyph(&glyph)                     │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ font_render_glyph(glyph, x=100, y=100, color=WHITE):   │
│   For each row in glyph.bitmap:                          │
│       For each pixel in row (bit = MSB to LSB):         │
│           if (bit set):                                  │
│               gfx_draw_pixel(&back_buffer, x+i, y+j,   │
│                             0xFFFFFFFF)  /* white */    │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ gfx_draw_pixel():                                         │
│   pixels[y * pitch + x] = 0xFFFFFFFF                    │
│                                                          │
│   (Back buffer in RAM updated with white pixel)         │
└──────────────────────────────────────────────────────────┘
                        │
     (Repeat for 'e', 'l', 'l', 'o', ...)                 │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ Application calls: gfx_present()                         │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ gfx_present():                                            │
│   memcpy(framebuffer_addr, back_buffer, size)           │
│                                                          │
│   (All changes displayed at once — no flicker!)         │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│ Monitor displays: "Hello, World!" on screen              │
└──────────────────────────────────────────────────────────┘
```

### Image Loading & Display

```
User code:
    gfx_draw_bmp_image(&ctx, 50, 50, "/mnt/photo.bmp")
    │
    ├─ fs_open("/mnt/photo.bmp")  ──────────────┐
    │                                           │
    ├─ Read BMP header                          │
    │   (magic, file size, pixel offset)        │
    │                                           │
    ├─ Read DIB header                          │
    │   (width, height, bits_per_pixel)         │
    │                                           │
    ├─ malloc(width × height × 4)  ◄──────────┐
    │   (Allocate temp ARGB buffer in RAM)     │
    │                                          │
    ├─ Seek to pixel data offset               │
    │                                          │
    ├─ For each pixel:                         │
    │   if (bits_per_pixel == 24):            │
    │       read 3 bytes (B, G, R)            │
    │       convert: ARGB = 0xFF000000 | (R << 16) | (G << 8) | B
    │       write to temp buffer
    │   else if (bits_per_pixel == 32):
    │       read 4 bytes (direct ARGB)
    │       write to temp buffer
    │   ...
    │
    ├─ Create graphics_canvas from temp buffer
    │
    ├─ gfx_blit_buffer(&ctx, 50, 50, &image_canvas, ...)
    │   (Composite pixels to back buffer)
    │   For each pixel in region:
    │       back_buffer[dst_y*pitch + dst_x] = image[...]
    │
    ├─ free(pixels)  ◄────────────────────────────┘
    │   (Delete temporary conversion from RAM)
    │   (Original /mnt/photo.bmp is NEVER touched)
    │
    └─ fs_close()

Application calls:
    gfx_present()
    │
    └─ memcpy(hardware_framebuffer, back_buffer, size)
       (Display the composited result)
```

---

## 9. Future Enhancements (Not Blocking)

### Clipping Rectangles
```c
void gfx_set_clip_rect(graphics_context_t *ctx, u32 x, u32 y, u32 w, u32 h);

/* All subsequent draws are clipped to this rect */
gfx_fill_rect(ctx, 0, 0, 1000, 1000, RED);  /* Only visible in clip region */
```

### Dirty Rectangle Tracking
```c
/* Instead of copying entire back buffer, only update changed regions */
void gfx_present_dirty(const rect_t *dirty_rects, u32 count);
```

### Sprite/Tilemap Support
```c
typedef struct {
    graphics_canvas_t tiles[256];  /* Tileset */
    u16 tilemap[width][height];    /* Tile indices */
} tilemap_t;

void gfx_draw_tilemap(graphics_context_t *ctx, const tilemap_t *map, u32 x, u32 y);
```

### Viewport/Scrolling
```c
typedef struct {
    u32 scroll_x, scroll_y;
    u32 visible_w, visible_h;
} viewport_t;

void gfx_draw_with_viewport(graphics_context_t *ctx, viewport_t *vp, ...);
```

---

## 10. Common Patterns & Examples

### Drawing a Button

```c
void draw_button(graphics_context_t *ctx, u32 x, u32 y, u32 w, u32 h,
                 const char *label) {
    /* Background */
    gfx_fill_rect(ctx, x, y, w, h, 0xFF4444FF);  /* Blue */
    
    /* Border */
    gfx_draw_rect(ctx, x, y, w, h, 0xFFFFFFFF);  /* White outline */
    
    /* Label text */
    text_renderer_t btn_text;
    text_init(&btn_text, ctx, &font_default, x+5, y+5, x+w-5, y+h-5);
    text_set_color(&btn_text, 0xFFFFFFFF);
    text_puts(&btn_text, label);
}
```

### Blending Sprites

```c
void draw_sprite_with_shadow(graphics_context_t *ctx,
                             const graphics_canvas_t *sprite,
                             u32 x, u32 y) {
    /* Draw shadow (semi-transparent black below) */
    graphics_canvas_t shadow = // ... create shadow canvas
    gfx_blit_alpha(ctx, x+2, y+2, &shadow, 0, 0, ..., 100);  /* 40% opaque */
    
    /* Draw sprite (fully opaque) */
    gfx_blit_buffer(ctx, x, y, sprite, 0, 0, sprite->width, sprite->height);
}
```

### Scrolling Text Region

```c
void scroll_log(text_renderer_t *log) {
    text_scroll(log);  /* Move all text up one line */
    
    /* Redraw entire region from scratch or maintain dirty region */
    gfx_present();
}
```

---

## 11. Debugging & Profiling

### Framebuffer Inspection

```c
/* Dump raw framebuffer pixel values to serial log */
void gfx_debug_dump_framebuffer(void) {
    u32 *fb = (u32 *)g_graphics.fb_info.addr;
    printk("FB addr: %p, pitch: %d, %dx%d\n",
           fb, g_graphics.fb_info.pitch,
           g_graphics.fb_info.width, g_graphics.fb_info.height);
           
    for (u32 y = 0; y < 10; y++) {  /* First 10 rows */
        for (u32 x = 0; x < 10; x++) {  /* First 10 cols */
            u32 pixel = fb[y * (g_graphics.fb_info.pitch/4) + x];
            printk("%08X ", pixel);
        }
        printk("\n");
    }
}
```

### Back Buffer Verification

```c
/* Verify back buffer was modified correctly */
void gfx_debug_dump_backbuffer(void) {
    graphics_canvas_t *bb = &g_graphics.back_buffer;
    printk("Back buffer: %p, %dx%d, pitch=%d\n",
           bb->pixels, bb->width, bb->height, bb->pitch);
    
    /* Show first pixel of first line */
    u32 first_pixel = bb->pixels[0];
    printk("Pixel [0,0] = 0x%08X\n", first_pixel);
}
```

---

## 12. Performance Considerations

| Operation | Bottleneck | Optimization |
|-----------|-----------|--------------|
| `gfx_present()` | Memcpy (full screen) | Dirty rectangle tracking |
| `gfx_fill_rect()` (large) | Per-pixel loop | SIMD/SSE fills (future) |
| Text rendering | Glyph lookups | Cache glyphs in VRAM (future) |
| Image loading | File I/O + conversion | Cache common images in RAM |
| Alpha blending | Pixel-wise computation | Fixed-function blend (hardware, future) |

**Current Focus**: Correctness over speed. Optimize once requirements are clear.

---

## 13. Summary: You Now Have...

✅ A **layered architecture** that separates concerns  
✅ A **software renderer** with primitives and compositing  
✅ A **text rendering system** that plugs into printk  
✅ A **BMP image pipeline** that respects file integrity  
✅ A **double-buffering system** to eliminate flicker  
✅ A **clear upgrade path** to TTF fonts, windows, and more  
✅ A **pixel-grid philosophy** (dot-matrix like)  

**Next Steps**:  
1. Implement `gfx_renderer.c` with core primitives  
2. Create graphics context and back buffer management  
3. Integrate into printk as graphics console output  
4. Add image loading for BMP files  
5. Extend with UI elements as needed

