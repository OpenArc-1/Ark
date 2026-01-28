# Userspace Graphics Integration Guide

This guide extends the Ark kernel API to expose graphics functions to userspace, allowing programs like the graphics shell to render directly to the framebuffer.

---

## Overview

Currently, the kernel API (`ark_kernel_api_t`) provides:
- `printk` - logging
- Input functions (`input_has_key`, `input_getc`, `input_read`)
- Time (`read_rtc`)
- CPU info (`cpuid`, `get_cpu_vendor`)
- VFS functions (v2)
- TTY functions (v2)

**New Extensions** will add:
- Graphics renderer functions
- Text rendering functions
- Image loading functions

---

## Step 1: Extend `include/ark/init_api.h`

Add these function pointer types and extend the API structure:

```c
/* Add to init_api.h after existing includes */

/* Opaque graphics types (userspace doesn't need internal structure) */
typedef struct { void *_private; } gfx_context_t;
typedef struct { void *_private; } text_context_t;
typedef struct { void *_private; } gfx_canvas_t;

/* Graphics color (ARGB 32-bit) */
typedef u32 gfx_color_t;

/* Graphics API function pointers */
typedef struct {
    /* === GRAPHICS RENDERER === */
    
    /**
     * Get main graphics context (renders to back buffer)
     */
    gfx_context_t *(*gfx_get_context)(void);
    
    /**
     * Clear back buffer to color
     */
    void (*gfx_clear)(u32 color);
    
    /**
     * Fill solid rectangle
     * color is ARGB: 0xARRGGBB
     */
    void (*gfx_fill_rect)(gfx_context_t *ctx, u32 x, u32 y,
                          u32 w, u32 h, u32 color);
    
    /**
     * Draw rectangle outline
     */
    void (*gfx_draw_rect)(gfx_context_t *ctx, u32 x, u32 y,
                          u32 w, u32 h, u32 color);
    
    /**
     * Draw single pixel
     */
    void (*gfx_draw_pixel)(gfx_context_t *ctx, u32 x, u32 y, u32 color);
    
    /**
     * Draw line (Bresenham)
     */
    void (*gfx_draw_line)(gfx_context_t *ctx, s32 x0, s32 y0,
                          s32 x1, s32 y1, u32 color);
    
    /**
     * Draw circle (filled)
     */
    void (*gfx_fill_circle)(gfx_context_t *ctx, s32 cx, s32 cy,
                            u32 radius, u32 color);
    
    /**
     * Draw circle outline
     */
    void (*gfx_draw_circle)(gfx_context_t *ctx, s32 cx, s32 cy,
                            u32 radius, u32 color);
    
    /**
     * Composite buffer (blit)
     */
    void (*gfx_blit_buffer)(gfx_context_t *dst, u32 dst_x, u32 dst_y,
                            const gfx_canvas_t *src, u32 src_x, u32 src_y,
                            u32 w, u32 h);
    
    /**
     * Flush back buffer to display (required for changes to be visible)
     */
    void (*gfx_present)(void);
    
    /**
     * Set clipping rectangle
     */
    void (*gfx_set_clip_rect)(gfx_context_t *ctx, u32 x, u32 y, u32 w, u32 h);
    
    /**
     * Get screen dimensions (in pixels)
     */
    void (*gfx_get_dimensions)(u32 *width, u32 *height);
    
    /* === TEXT RENDERING === */
    
    /**
     * Get text rendering context
     */
    text_context_t *(*text_get_context)(void);
    
    /**
     * Output string to text context
     */
    void (*text_puts)(text_context_t *ctx, const char *str);
    
    /**
     * Output single character
     */
    void (*text_putc)(text_context_t *ctx, char c);
    
    /**
     * Set text color
     */
    void (*text_set_color)(text_context_t *ctx, u32 color);
    
    /**
     * Clear text area
     */
    void (*text_clear)(text_context_t *ctx);
    
    /* === IMAGE LOADING === */
    
    /**
     * Load and display BMP image
     * Returns 0 on success, -1 on error
     */
    int (*image_draw_bmp)(gfx_context_t *ctx, u32 x, u32 y, const char *path);
    
    /**
     * Get last image loading error string
     */
    const char *(*image_get_error)(void);
    
} ark_graphics_api_t;

/* Extend main API structure */
struct ark_kernel_api {
    /* ... existing fields ... */
    
    /* Version 2 additions remain */
    
    /* Version 3: Graphics support */
    ark_graphics_api_t *graphics;   /* NULL if not available */
};

/* Update version */
#define ARK_INIT_API_VERSION 3
```

---

## Step 2: Add Kernel API Exports

Modify `gen/init_api.c` to expose graphics functions:

```c
/* At top of init_api.c, after existing includes */

#include "ark/gfx_renderer.h"
#include "ark/text_renderer.h"
#include "ark/image_loader.h"

/* ============ Graphics API Wrappers ============ */

/* These are thin wrappers around kernel graphics functions */

static gfx_context_t *wrapper_gfx_get_context(void) {
    return (gfx_context_t *)gfx_get_context();
}

static void wrapper_gfx_clear(u32 color) {
    gfx_clear(color);
}

static void wrapper_gfx_fill_rect(gfx_context_t *ctx, u32 x, u32 y,
                                   u32 w, u32 h, u32 color) {
    gfx_fill_rect((gfx_context_t *)ctx, x, y, w, h, color);
}

static void wrapper_gfx_draw_rect(gfx_context_t *ctx, u32 x, u32 y,
                                   u32 w, u32 h, u32 color) {
    gfx_draw_rect((gfx_context_t *)ctx, x, y, w, h, color);
}

static void wrapper_gfx_draw_pixel(gfx_context_t *ctx, u32 x, u32 y, u32 color) {
    gfx_draw_pixel((gfx_context_t *)ctx, x, y, color);
}

static void wrapper_gfx_draw_line(gfx_context_t *ctx, s32 x0, s32 y0,
                                   s32 x1, s32 y1, u32 color) {
    gfx_draw_line((gfx_context_t *)ctx, x0, y0, x1, y1, color);
}

static void wrapper_gfx_fill_circle(gfx_context_t *ctx, s32 cx, s32 cy,
                                     u32 radius, u32 color) {
    gfx_fill_circle((gfx_context_t *)ctx, cx, cy, radius, color);
}

static void wrapper_gfx_draw_circle(gfx_context_t *ctx, s32 cx, s32 cy,
                                     u32 radius, u32 color) {
    gfx_draw_circle((gfx_context_t *)ctx, cx, cy, radius, color);
}

static void wrapper_gfx_blit_buffer(gfx_context_t *dst, u32 dst_x, u32 dst_y,
                                     const gfx_canvas_t *src, u32 src_x, u32 src_y,
                                     u32 w, u32 h) {
    gfx_blit_buffer((gfx_context_t *)dst, dst_x, dst_y,
                    (const gfx_canvas_t *)src, src_x, src_y, w, h);
}

static void wrapper_gfx_present(void) {
    gfx_present();
}

static void wrapper_gfx_set_clip_rect(gfx_context_t *ctx, u32 x, u32 y, u32 w, u32 h) {
    gfx_set_clip_rect((gfx_context_t *)ctx, x, y, w, h);
}

static void wrapper_gfx_get_dimensions(u32 *width, u32 *height) {
    if (!width || !height) return;
    
    gfx_context_t *ctx = gfx_get_context();
    if (ctx) {
        *width = ctx->canvas.width;
        *height = ctx->canvas.height;
    } else {
        *width = 0;
        *height = 0;
    }
}

static text_context_t *wrapper_text_get_context(void) {
    /* Assuming kernel has a global text context for graphics console */
    extern text_context_t g_kernel_text_context;
    return (text_context_t *)&g_kernel_text_context;
}

static void wrapper_text_puts(text_context_t *ctx, const char *str) {
    text_puts((text_context_t *)ctx, str);
}

static void wrapper_text_putc(text_context_t *ctx, char c) {
    text_putc((text_context_t *)ctx, c);
}

static void wrapper_text_set_color(text_context_t *ctx, u32 color) {
    text_set_color((text_context_t *)ctx, color);
}

static void wrapper_text_clear(text_context_t *ctx) {
    text_clear((text_context_t *)ctx);
}

static int wrapper_image_draw_bmp(gfx_context_t *ctx, u32 x, u32 y,
                                   const char *path) {
    int error;
    image_draw_bmp((gfx_context_t *)ctx, x, y, path);
    return 0;  /* Success */
}

static const char *g_last_image_error = "OK";

static const char *wrapper_image_get_error(void) {
    return g_last_image_error;
}

/* ============ Build Graphics API Table ============ */

static const ark_graphics_api_t g_graphics_api = {
    .gfx_get_context = wrapper_gfx_get_context,
    .gfx_clear = wrapper_gfx_clear,
    .gfx_fill_rect = wrapper_gfx_fill_rect,
    .gfx_draw_rect = wrapper_gfx_draw_rect,
    .gfx_draw_pixel = wrapper_gfx_draw_pixel,
    .gfx_draw_line = wrapper_gfx_draw_line,
    .gfx_fill_circle = wrapper_gfx_fill_circle,
    .gfx_draw_circle = wrapper_gfx_draw_circle,
    .gfx_blit_buffer = wrapper_gfx_blit_buffer,
    .gfx_present = wrapper_gfx_present,
    .gfx_set_clip_rect = wrapper_gfx_set_clip_rect,
    .gfx_get_dimensions = wrapper_gfx_get_dimensions,
    
    .text_get_context = wrapper_text_get_context,
    .text_puts = wrapper_text_puts,
    .text_putc = wrapper_text_putc,
    .text_set_color = wrapper_text_set_color,
    .text_clear = wrapper_text_clear,
    
    .image_draw_bmp = wrapper_image_draw_bmp,
    .image_get_error = wrapper_image_get_error,
};

/* ============ Update Main API ============ */

/* Modify ark_kernel_api() function to include graphics */

const ark_kernel_api_t *ark_kernel_api(void) {
    static ark_kernel_api_t api = {
        /* ... existing fields ... */
        .graphics = (ark_graphics_api_t *)&g_graphics_api,  /* NEW */
    };
    return &api;
}
```

---

## Step 3: Use Graphics in Userspace

Now userspace programs can use graphics:

```c
/* In userspace/graphics_shell.c */

int _start(const ark_kernel_api_t *api) {
    if (!api || api->version < 3) {
        api->printk("[shell] Need API v3 for graphics\n");
        return 1;
    }
    
    if (!api->graphics) {
        api->printk("[shell] Graphics API not available\n");
        return 1;
    }
    
    /* Get graphics context */
    gfx_context_t *ctx = api->graphics->gfx_get_context();
    
    /* Clear screen to blue */
    api->graphics->gfx_clear(0xFF0000FF);
    
    /* Draw a red rectangle */
    api->graphics->gfx_fill_rect(ctx, 100, 100, 200, 150, 0xFFFF0000);
    
    /* Draw a white border */
    api->graphics->gfx_draw_rect(ctx, 100, 100, 200, 150, 0xFFFFFFFF);
    
    /* Display the result */
    api->graphics->gfx_present();
    
    return 0;
}
```

---

## Step 4: Color Format Reference

Colors are 32-bit ARGB:

```c
#define ARGB(a,r,g,b)  (((u32)(a)<<24) | ((u32)(r)<<16) | ((u32)(g)<<8) | (u32)(b))

/* Common colors */
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_CYAN      0xFF00FFFF
#define COLOR_MAGENTA   0xFFFF00FF
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_GRAY      0xFF808080

/* Semi-transparent (example) */
#define COLOR_RED_50    0x80FF0000  /* 50% opaque red */
```

---

## Step 5: Build System Integration

Update `Makefile` to compile new modules:

```makefile
# Graphics modules
GFX_OBJS := gen/gfx_renderer.o \
            gen/text_renderer.o \
            gen/image_loader.o

# Add to kernel image
OBJS += $(GFX_OBJS)

# Userspace: choose shell or graphics_shell
USERSPACE_OBJS := userspace/graphics_shell.o  # Instead of userspace/init.o
```

---

## Example: Complete Graphics Application

```c
#include "ark/init_api.h"

int _start(const ark_kernel_api_t *api) {
    if (!api || api->version < 3 || !api->graphics) {
        api->printk("Need graphics API\n");
        return 1;
    }
    
    gfx_context_t *ctx = api->graphics->gfx_get_context();
    u32 width, height;
    
    api->graphics->gfx_get_dimensions(&width, &height);
    
    /* --- Render Frame --- */
    
    /* Background */
    api->graphics->gfx_clear(0xFF1a1a2e);  /* Dark blue */
    
    /* Title bar */
    api->graphics->gfx_fill_rect(ctx, 0, 0, width, 60, 0xFF0f3460);
    api->graphics->gfx_draw_rect(ctx, 0, 0, width, 60, 0xFFFFFFFF);
    
    /* Content area */
    api->graphics->gfx_fill_rect(ctx, 10, 80, width-20, height-100, 0xFF16213e);
    api->graphics->gfx_draw_rect(ctx, 10, 80, width-20, height-100, 0xFF00d4ff);
    
    /* Buttons */
    api->graphics->gfx_fill_rect(ctx, 20, height-60, 100, 40, 0xFF0f3460);
    api->graphics->gfx_draw_rect(ctx, 20, height-60, 100, 40, 0xFF00d4ff);
    
    /* Get text context */
    text_context_t *txt = api->graphics->text_get_context();
    api->graphics->text_set_color(txt, 0xFFFFFFFF);
    api->graphics->text_puts(txt, "Graphics Application");
    
    /* Load image (requires BMP) */
    api->graphics->image_draw_bmp(ctx, 50, 150, "/images/logo.bmp");
    
    /* Display */
    api->graphics->gfx_present();
    
    /* Wait for input */
    api->printk("Press any key to exit...\n");
    api->input_getc();
    
    return 0;
}
```

---

## Implementation Checklist

To enable graphics in userspace:

1. **Kernel Side:**
   - [ ] Implement graphics renderer (`gfx_renderer.c`)
   - [ ] Implement text rendering (`text_renderer.c`)
   - [ ] Implement image loader (`image_loader.c`)
   - [ ] Extend `init_api.h` with graphics types and API
   - [ ] Add graphics wrappers to `init_api.c`
   - [ ] Build graphics API table
   - [ ] Update kernel API version to 3
   - [ ] Test graphics initialization in kernel

2. **Userspace Side:**
   - [ ] Update Makefile to use `graphics_shell.c`
   - [ ] Uncomment graphics calls in `graphics_shell.c`
   - [ ] Test: render welcome screen
   - [ ] Test: handle user input
   - [ ] Test: command execution

3. **Testing:**
   - [ ] Boot system with graphics enabled
   - [ ] Verify shell renders correctly
   - [ ] Test drawing primitives (rect, circle, line)
   - [ ] Test text rendering
   - [ ] Test image display
   - [ ] Verify no memory leaks

---

## Common Patterns

### Draw a Button

```c
api->graphics->gfx_fill_rect(ctx, x, y, w, h, 0xFF0066FF);    /* Fill */
api->graphics->gfx_draw_rect(ctx, x, y, w, h, 0xFFFFFFFF);    /* Border */
/* Text would go here */
api->graphics->gfx_present();
```

### Draw Multiple Objects

```c
/* Draw background */
api->graphics->gfx_clear(0xFF000000);

/* Draw components */
api->graphics->gfx_fill_circle(ctx, 200, 200, 50, 0xFFFF0000);
api->graphics->gfx_draw_rect(ctx, 300, 100, 100, 100, 0xFF00FF00);
api->graphics->gfx_draw_line(ctx, 0, 0, 800, 600, 0xFFFFFFFF);

/* Display all at once */
api->graphics->gfx_present();
```

### Load Image

```c
int result = api->graphics->image_draw_bmp(ctx, 100, 100, "/images/test.bmp");
if (result != 0) {
    const char *err = api->graphics->image_get_error();
    api->printk("Image error: %s\n", err);
}
api->graphics->gfx_present();
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Graphics null pointer | Check API version ≥ 3 and graphics != NULL |
| Nothing displays | Call `gfx_present()` to flush |
| Flicker between frames | Draw all elements, then single `gfx_present()` |
| Text doesn't appear | Check text context is valid, call `gfx_present()` |
| Image won't load | Verify BMP file exists in VFS and is uncompressed |

---

This design allows userspace programs to have full graphics capabilities while maintaining kernel security through the API abstraction layer!

