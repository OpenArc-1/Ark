# Graphics Architecture - Image Pipeline Implementation

Complete guide for loading, converting, and displaying BMP images without decompressing JPEG/PNG.

---

## Part 1: BMP File Format (Decoder)

### BMP Structure Overview

```
BMP File Layout:

┌─────────────────────────────────────┐
│   BMP File Header (14 bytes)        │
│  - Magic: "BM" (0x4D42)             │
│  - File size                        │
│  - Reserved                         │
│  - Pixel data offset                │
├─────────────────────────────────────┤
│   DIB Header (variable, typically   │
│   40 bytes for BITMAPINFOHEADER)    │
│  - Width, Height                    │
│  - Bits per pixel                   │
│  - Compression type                 │
│  - Color palette (if applicable)    │
├─────────────────────────────────────┤
│   Color Table (optional)            │
│  - For indexed color modes (1,4,8)  │
├─────────────────────────────────────┤
│   Pixel Data (packed bits/bytes)    │
│  - Stored bottom-to-top typically   │
│  - Padded to 4-byte alignment       │
└─────────────────────────────────────┘
```

### BMP File Header

```c
#pragma pack(1)
typedef struct {
    u16 magic;           /* 'BM' = 0x4D42 */
    u32 file_size;       /* Total file size in bytes */
    u16 reserved1;       /* Unused */
    u16 reserved2;       /* Unused */
    u32 pixel_offset;    /* Offset to pixel data start */
} bmp_file_header_t;
#pragma pack()

/* Size = 14 bytes */
```

### BMP DIB Header (BITMAPINFOHEADER)

```c
#pragma pack(1)
typedef struct {
    u32 header_size;          /* 40 for BITMAPINFOHEADER */
    s32 width;                /* Width in pixels */
    s32 height;               /* Height: positive=bottom-up, negative=top-down */
    u16 planes;               /* Always 1 */
    u16 bits_per_pixel;       /* 1, 4, 8, 16, 24, or 32 */
    u32 compression;          /* 0=none, 1=RLE4, 2=RLE8, 3=BITFIELDS, 4=JPEG, 5=PNG */
    u32 image_size;           /* Image data size (0 if uncompressed) */
    s32 x_pixels_per_meter;   /* Print resolution */
    s32 y_pixels_per_meter;   /* Print resolution */
    u32 colors_used;          /* Number of colors in palette (0=default) */
    u32 colors_important;     /* Number of colors used (0=all) */
} bmp_dib_header_t;
#pragma pack()

/* Size = 40 bytes */
```

### Color Table (Palette)

For 1, 4, or 8-bit images, a color palette follows the header:

```c
#pragma pack(1)
typedef struct {
    u8 blue;      /* B */
    u8 green;     /* G */
    u8 red;       /* R */
    u8 reserved;  /* Leave 0 */
} bmp_color_entry_t;
#pragma pack()

/*
 * For 8-bit images: 256 entries = 1024 bytes
 * For 4-bit images: 16 entries = 64 bytes  
 * For 1-bit images: 2 entries = 8 bytes
 */
```

---

## Part 2: Image Loader Header

### `include/ark/image_loader.h`

```c
/**
 * Image Loading and Display
 * 
 * Provides BMP loading and high-level image display.
 * Images are loaded to temporary RAM buffers, displayed, then freed.
 * Original files are never modified.
 */

#pragma once

#include "ark/gfx_renderer.h"
#include "ark/types.h"

/* BMP file format structures */
#pragma pack(1)

typedef struct {
    u16 magic;           /* 'BM' = 0x4D42 */
    u32 file_size;
    u16 reserved1;
    u16 reserved2;
    u32 pixel_offset;
} bmp_file_header_t;

typedef struct {
    u32 header_size;
    s32 width;
    s32 height;          /* Positive = bottom-up, Negative = top-down */
    u16 planes;
    u16 bits_per_pixel;  /* 1, 4, 8, 16, 24, 32 */
    u32 compression;     /* 0 = no compression */
    u32 image_size;
    s32 x_pixels_per_meter;
    s32 y_pixels_per_meter;
    u32 colors_used;
    u32 colors_important;
} bmp_dib_header_t;

typedef struct {
    u8 blue;
    u8 green;
    u8 red;
    u8 reserved;
} bmp_color_entry_t;

#pragma pack()

/* Error codes */
#define IMG_ERROR_OK              0
#define IMG_ERROR_FILE_NOT_FOUND  1
#define IMG_ERROR_INVALID_FORMAT  2
#define IMG_ERROR_OUT_OF_MEMORY   3
#define IMG_ERROR_UNSUPPORTED_BPP 4
#define IMG_ERROR_COMPRESSED      5

/* ============ Low-level BMP Loading ============ */

/**
 * Load BMP file and convert to ARGB canvas.
 * The returned canvas is allocated in RAM and must be freed with
 * gfx_free_canvas() after use.
 * 
 * Returns: pointer to canvas, or NULL on error.
 * error_out: optional pointer to receive error code.
 */
gfx_canvas_t *image_load_bmp(const char *filepath, int *error_out);

/**
 * Load BMP from an open file descriptor.
 * Useful for VFS integration.
 * file_fd: file descriptor or handle (opaque)
 */
gfx_canvas_t *image_load_bmp_fd(void *file_fd, int *error_out);

/* ============ High-level Image Display ============ */

/**
 * Load and display a BMP image at screen position (x, y).
 * The image is loaded, composited to back buffer, and freed.
 */
void image_draw_bmp(gfx_context_t *ctx, u32 x, u32 y,
                    const char *bmp_filepath);

/**
 * Display a canvas (pre-loaded image) at screen position.
 */
void image_draw_canvas(gfx_context_t *ctx, u32 x, u32 y,
                       const gfx_canvas_t *canvas);

/**
 * Get last image loading error as human-readable string.
 */
const char *image_get_error_string(int error_code);

/* ============ Image Conversion Helpers ============ */

/**
 * Convert indexed pixel to ARGB using palette.
 * index: palette entry (0-255)
 * palette: pointer to palette entries
 */
u32 image_palette_to_argb(u8 index, const u32 *palette);

/**
 * Convert 24-bit BGR to 32-bit ARGB.
 */
u32 image_bgr24_to_argb(u8 b, u8 g, u8 r);

/**
 * Convert 16-bit 5-5-5 RGB to 32-bit ARGB.
 */
u32 image_rgb555_to_argb(u16 rgb555);

/**
 * Convert 16-bit 5-6-5 RGB to 32-bit ARGB.
 */
u32 image_rgb565_to_argb(u16 rgb565);

#endif /* IMAGE_LOADER_H */
```

---

## Part 3: Image Loader Implementation

### `gen/image_loader.c`

```c
/**
 * BMP Image Loading Implementation
 *
 * Handles loading BMP files into ARGB canvas buffers.
 * Supports multiple color depths (1, 4, 8, 16, 24, 32-bit).
 * Manages temporary RAM allocation for conversions.
 */

#include "ark/image_loader.h"
#include "ark/printk.h"
#include <string.h>
#include <stdlib.h>

/* Error messages */
static const char *error_messages[] = {
    "OK",
    "File not found",
    "Invalid BMP format",
    "Out of memory",
    "Unsupported bits per pixel",
    "Compressed BMP files not supported",
};

const char *image_get_error_string(int error_code) {
    if (error_code < 0 || error_code >= (int)(sizeof(error_messages)/sizeof(error_messages[0]))) {
        return "Unknown error";
    }
    return error_messages[error_code];
}

/* ============ File I/O Helpers ============ */

/**
 * Read from file (stub - actual implementation depends on VFS)
 * This is a placeholder - implement based on your VFS.
 */
static int file_read(void *file_fd, void *buffer, u32 size) {
    /* TODO: Implement VFS read */
    /* For now, assume file_fd is a FILE* pointer */
    return fread(buffer, 1, size, (FILE *)file_fd);
}

static void *file_open(const char *filepath) {
    /* TODO: Implement VFS open */
    return fopen(filepath, "rb");
}

static void file_close(void *file_fd) {
    /* TODO: Implement VFS close */
    if (file_fd) {
        fclose((FILE *)file_fd);
    }
}

static int file_seek(void *file_fd, u32 offset) {
    /* TODO: Implement VFS seek */
    return fseek((FILE *)file_fd, offset, SEEK_SET);
}

/* ============ Conversion Functions ============ */

u32 image_palette_to_argb(u8 index, const u32 *palette) {
    if (!palette) {
        return 0xFF000000;  /* Black */
    }
    return palette[index];
}

u32 image_bgr24_to_argb(u8 b, u8 g, u8 r) {
    return GFX_ARGB(0xFF, r, g, b);
}

u32 image_rgb555_to_argb(u16 rgb555) {
    u8 r = ((rgb555 >> 10) & 0x1F) << 3;  /* 5 bits to 8 bits */
    u8 g = ((rgb555 >> 5) & 0x1F) << 3;
    u8 b = ((rgb555 >> 0) & 0x1F) << 3;
    return GFX_ARGB(0xFF, r, g, b);
}

u32 image_rgb565_to_argb(u16 rgb565) {
    u8 r = ((rgb565 >> 11) & 0x1F) << 3;  /* 5 bits to 8 bits */
    u8 g = ((rgb565 >> 5) & 0x3F) << 2;   /* 6 bits to 8 bits */
    u8 b = ((rgb565 >> 0) & 0x1F) << 3;   /* 5 bits to 8 bits */
    return GFX_ARGB(0xFF, r, g, b);
}

/* ============ BMP Loading ============ */

gfx_canvas_t *image_load_bmp_fd(void *file_fd, int *error_out) {
    if (!file_fd) {
        if (error_out) *error_out = IMG_ERROR_FILE_NOT_FOUND;
        return NULL;
    }

    bmp_file_header_t file_hdr;
    bmp_dib_header_t dib_hdr;
    u32 palette_size = 0;
    u32 *palette = NULL;
    gfx_canvas_t *canvas = NULL;

    /* ========== Step 1: Read and validate file header ========== */
    if (file_read(file_fd, &file_hdr, sizeof(file_hdr)) != sizeof(file_hdr)) {
        if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
        printk("[img] Error: cannot read BMP file header\n");
        return NULL;
    }

    if (file_hdr.magic != 0x4D42) {  /* 'BM' */
        if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
        printk("[img] Error: not a valid BMP file (magic = 0x%04X)\n",
               file_hdr.magic);
        return NULL;
    }

    /* ========== Step 2: Read DIB header ========== */
    if (file_read(file_fd, &dib_hdr, sizeof(dib_hdr)) != sizeof(dib_hdr)) {
        if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
        printk("[img] Error: cannot read DIB header\n");
        return NULL;
    }

    /* Validate dimensions */
    if (dib_hdr.width <= 0 || dib_hdr.height == 0) {
        if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
        printk("[img] Error: invalid BMP dimensions (%d x %d)\n",
               dib_hdr.width, dib_hdr.height);
        return NULL;
    }

    /* Check for compression (we don't support compressed formats) */
    if (dib_hdr.compression != 0) {
        if (error_out) *error_out = IMG_ERROR_COMPRESSED;
        printk("[img] Error: compressed BMP not supported (compression = %d)\n",
               dib_hdr.compression);
        return NULL;
    }

    /* ========== Step 3: Validate bits per pixel ========== */
    switch (dib_hdr.bits_per_pixel) {
    case 1:
    case 4:
    case 8:
    case 16:
    case 24:
    case 32:
        break;
    default:
        if (error_out) *error_out = IMG_ERROR_UNSUPPORTED_BPP;
        printk("[img] Error: unsupported bits per pixel (%d)\n",
               dib_hdr.bits_per_pixel);
        return NULL;
    }

    /* ========== Step 4: Load color palette (if needed) ========== */
    if (dib_hdr.bits_per_pixel <= 8) {
        /* Calculate palette size */
        if (dib_hdr.colors_used > 0) {
            palette_size = dib_hdr.colors_used;
        } else {
            palette_size = 1 << dib_hdr.bits_per_pixel;  /* 2^bpp */
        }

        /* Allocate palette buffer */
        palette = malloc(palette_size * sizeof(u32));
        if (!palette) {
            if (error_out) *error_out = IMG_ERROR_OUT_OF_MEMORY;
            printk("[img] Error: out of memory for palette (%d entries)\n",
                   palette_size);
            return NULL;
        }

        /* Read palette entries (BGR format in file) */
        for (u32 i = 0; i < palette_size; i++) {
            bmp_color_entry_t entry;
            if (file_read(file_fd, &entry, sizeof(entry)) != sizeof(entry)) {
                if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
                free(palette);
                return NULL;
            }
            
            /* Convert BGR to ARGB */
            palette[i] = image_bgr24_to_argb(entry.blue, entry.green, entry.red);
        }
    }

    /* ========== Step 5: Allocate output canvas ========== */
    u32 width = (u32)dib_hdr.width;
    u32 height = (u32)abs(dib_hdr.height);

    canvas = gfx_create_canvas(width, height);
    if (!canvas) {
        if (error_out) *error_out = IMG_ERROR_OUT_OF_MEMORY;
        if (palette) free(palette);
        printk("[img] Error: out of memory for canvas (%u x %u)\n",
               width, height);
        return NULL;
    }

    /* ========== Step 6: Seek to pixel data ========== */
    file_seek(file_fd, file_hdr.pixel_offset);

    /* ========== Step 7: Read and convert pixel data ========== */
    u8 bottom_up = (dib_hdr.height > 0) ? 1 : 0;

    for (u32 y = 0; y < height; y++) {
        u32 dst_y = bottom_up ? (height - 1 - y) : y;
        u32 *dst_row = (u32 *)((u8 *)canvas->pixels + dst_y * canvas->pitch);

        switch (dib_hdr.bits_per_pixel) {
        case 32: {
            /* Direct ARGB (or BGRA) */
            for (u32 x = 0; x < width; x++) {
                u8 b, g, r, a;
                if (file_read(file_fd, &b, 1) != 1 ||
                    file_read(file_fd, &g, 1) != 1 ||
                    file_read(file_fd, &r, 1) != 1 ||
                    file_read(file_fd, &a, 1) != 1) {
                    gfx_free_canvas(canvas);
                    if (palette) free(palette);
                    if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
                    return NULL;
                }
                dst_row[x] = GFX_ARGB(a, r, g, b);
            }
            break;
        }

        case 24: {
            /* BGR format (3 bytes per pixel) */
            for (u32 x = 0; x < width; x++) {
                u8 b, g, r;
                if (file_read(file_fd, &b, 1) != 1 ||
                    file_read(file_fd, &g, 1) != 1 ||
                    file_read(file_fd, &r, 1) != 1) {
                    gfx_free_canvas(canvas);
                    if (palette) free(palette);
                    if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
                    return NULL;
                }
                dst_row[x] = image_bgr24_to_argb(b, g, r);
            }
            
            /* Pad to 4-byte boundary */
            u32 padding = (4 - (width * 3) % 4) % 4;
            file_seek(file_fd, file_seek(file_fd, 0) + padding);
            break;
        }

        case 16: {
            /* 16-bit RGB (either 5-5-5 or 5-6-5) */
            for (u32 x = 0; x < width; x++) {
                u16 pixel;
                u8 lo, hi;
                if (file_read(file_fd, &lo, 1) != 1 ||
                    file_read(file_fd, &hi, 1) != 1) {
                    gfx_free_canvas(canvas);
                    if (palette) free(palette);
                    if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
                    return NULL;
                }
                pixel = lo | ((u16)hi << 8);
                dst_row[x] = image_rgb565_to_argb(pixel);
            }
            
            /* Pad to 4-byte boundary */
            u32 padding = (4 - (width * 2) % 4) % 4;
            file_seek(file_fd, file_seek(file_fd, 0) + padding);
            break;
        }

        case 8: {
            /* 8-bit indexed color */
            for (u32 x = 0; x < width; x++) {
                u8 index;
                if (file_read(file_fd, &index, 1) != 1) {
                    gfx_free_canvas(canvas);
                    if (palette) free(palette);
                    if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
                    return NULL;
                }
                dst_row[x] = palette ? palette[index] : 0xFF000000;
            }
            
            /* Pad to 4-byte boundary */
            u32 padding = (4 - width % 4) % 4;
            file_seek(file_fd, file_seek(file_fd, 0) + padding);
            break;
        }

        case 4: {
            /* 4-bit indexed color (2 pixels per byte) */
            for (u32 x = 0; x < width; x += 2) {
                u8 byte;
                if (file_read(file_fd, &byte, 1) != 1) {
                    gfx_free_canvas(canvas);
                    if (palette) free(palette);
                    if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
                    return NULL;
                }
                
                u8 idx1 = (byte >> 4) & 0x0F;
                u8 idx2 = (byte >> 0) & 0x0F;
                
                dst_row[x] = palette ? palette[idx1] : 0xFF000000;
                if (x + 1 < width) {
                    dst_row[x + 1] = palette ? palette[idx2] : 0xFF000000;
                }
            }
            
            /* Pad to 4-byte boundary */
            u32 bytes_per_row = (width + 1) / 2;
            u32 padding = (4 - bytes_per_row % 4) % 4;
            file_seek(file_fd, file_seek(file_fd, 0) + padding);
            break;
        }

        case 1: {
            /* 1-bit monochrome (8 pixels per byte) */
            for (u32 x = 0; x < width; x += 8) {
                u8 byte;
                if (file_read(file_fd, &byte, 1) != 1) {
                    gfx_free_canvas(canvas);
                    if (palette) free(palette);
                    if (error_out) *error_out = IMG_ERROR_INVALID_FORMAT;
                    return NULL;
                }
                
                for (u32 b = 0; b < 8 && x + b < width; b++) {
                    u8 bit = (byte >> (7 - b)) & 1;
                    u8 idx = bit;
                    dst_row[x + b] = palette ? palette[idx] : 
                                      (bit ? 0xFFFFFFFF : 0xFF000000);
                }
            }
            
            /* Pad to 4-byte boundary */
            u32 bytes_per_row = (width + 7) / 8;
            u32 padding = (4 - bytes_per_row % 4) % 4;
            file_seek(file_fd, file_seek(file_fd, 0) + padding);
            break;
        }
        }
    }

    if (error_out) *error_out = IMG_ERROR_OK;
    if (palette) free(palette);

    printk("[img] Loaded BMP: %ux%u, %d bpp\n",
           width, height, dib_hdr.bits_per_pixel);

    return canvas;
}

gfx_canvas_t *image_load_bmp(const char *filepath, int *error_out) {
    if (!filepath) {
        if (error_out) *error_out = IMG_ERROR_FILE_NOT_FOUND;
        return NULL;
    }

    void *file_fd = file_open(filepath);
    if (!file_fd) {
        if (error_out) *error_out = IMG_ERROR_FILE_NOT_FOUND;
        printk("[img] Error: cannot open %s\n", filepath);
        return NULL;
    }

    gfx_canvas_t *canvas = image_load_bmp_fd(file_fd, error_out);
    file_close(file_fd);

    return canvas;
}

/* ============ High-level Display Functions ============ */

void image_draw_canvas(gfx_context_t *ctx, u32 x, u32 y,
                       const gfx_canvas_t *canvas) {
    if (!ctx || !canvas) {
        return;
    }

    gfx_blit_buffer(ctx, x, y, canvas, 0, 0,
                    canvas->width, canvas->height);
}

void image_draw_bmp(gfx_context_t *ctx, u32 x, u32 y,
                    const char *bmp_filepath) {
    if (!ctx || !bmp_filepath) {
        return;
    }

    int error;
    gfx_canvas_t *img = image_load_bmp(bmp_filepath, &error);
    
    if (!img) {
        printk("[img] Failed to load %s: %s\n",
               bmp_filepath, image_get_error_string(error));
        return;
    }

    /* Composite to back buffer */
    image_draw_canvas(ctx, x, y, img);

    /* CRITICAL: Free temporary buffer immediately */
    gfx_free_canvas(img);
    
    /* Original file is never modified */
}
```

---

## Part 4: Complete Image Pipeline Example

### Example: Displaying Multiple Images

```c
void demo_image_display(void) {
    gfx_context_t *ctx = gfx_get_context();

    /* Clear screen */
    gfx_clear(GFX_COLOR_BLACK);

    /* Show images at different positions */
    image_draw_bmp(ctx, 10, 10, "/mnt/vfs/logo.bmp");    /* Logo   */
    image_draw_bmp(ctx, 200, 10, "/mnt/vfs/photo.bmp");  /* Photo  */
    image_draw_bmp(ctx, 10, 200, "/mnt/vfs/icon.bmp");   /* Icon   */

    /* Add some text overlay */
    text_context_t txt;
    text_init(&txt, ctx, &text_font_default, 10, 300, 400, 400);
    text_set_color(&txt, GFX_COLOR_WHITE);
    text_puts(&txt, "Images loaded and displayed!");

    /* Display everything at once */
    gfx_present();
}
```

### Example: Image + Graphic Overlay

```c
void demo_image_with_graphics(void) {
    gfx_context_t *ctx = gfx_get_context();

    gfx_clear(GFX_COLOR_BLACK);

    /* Load background image */
    image_draw_bmp(ctx, 0, 0, "/mnt/vfs/background.bmp");

    /* Draw graphics overlay */
    gfx_fill_rect(ctx, 50, 50, 100, 100, GFX_ARGB(0x80, 0xFF, 0, 0));  /* Semi-transparent red */
    gfx_draw_circle(ctx, 200, 150, 50, GFX_COLOR_GREEN);

    /* Add text */
    text_context_t txt;
    text_init(&txt, ctx, &text_font_default, 10, 300, 400, 400);
    text_set_color(&txt, GFX_COLOR_YELLOW);
    text_puts(&txt, "Overlaid with graphics");

    gfx_present();
}
```

---

## Part 5: VFS Integration

### File I/O Stub (Modify `image_loader.c`)

```c
/* Replace the file I/O stubs with actual VFS calls */

#include "ark/vfs.h"  /* Your VFS header */

static int file_read(void *file_fd, void *buffer, u32 size) {
    if (!file_fd || !buffer) return 0;
    
    /* Assuming vfs_read returns bytes read */
    vfs_file_t *fp = (vfs_file_t *)file_fd;
    return vfs_read(fp, buffer, size);
}

static void *file_open(const char *filepath) {
    if (!filepath) return NULL;
    
    vfs_file_t *fp = malloc(sizeof(vfs_file_t));
    if (!fp) return NULL;
    
    if (!vfs_open(filepath, VFS_READ, fp)) {
        free(fp);
        return NULL;
    }
    
    return fp;
}

static void file_close(void *file_fd) {
    if (!file_fd) return;
    
    vfs_file_t *fp = (vfs_file_t *)file_fd;
    vfs_close(fp);
    free(fp);
}

static int file_seek(void *file_fd, u32 offset) {
    if (!file_fd) return -1;
    
    vfs_file_t *fp = (vfs_file_t *)file_fd;
    return vfs_seek(fp, offset, VFS_SEEK_SET);
}
```

---

## Part 6: Memory & Performance

### Memory Layout Example (480x640 Image)

```
Original file on disk: logo.bmp
    Size: ~900 KB (if 24-bit color)
    Location: /mnt/vfs/logo.bmp
    Status: NEVER MODIFIED

Loading process:
    1. Open file → file_fd
    2. Read headers → 54 bytes on stack
    3. Allocate pixel buffer → malloc(480 * 640 * 4) = ~1.2 MB in RAM
    4. Read and convert pixels → to allocated buffer
    5. Create canvas structure → points to buffer
    6. Display: gfx_blit_buffer() copies to back buffer
    7. Free pixel buffer → free() releases ~1.2 MB
    8. Close file → still untouched

Peak memory usage: ~1.2 MB (for temporary conversion)
File system state: unchanged
```

### Optimization: Cache Frequently Used Images

```c
/* Optional: maintain a small cache of last few loaded images */
#define IMAGE_CACHE_SIZE 3

typedef struct {
    gfx_canvas_t *canvas;
    char filepath[256];
    u32 last_used_tick;
} cached_image_t;

static cached_image_t g_image_cache[IMAGE_CACHE_SIZE];

gfx_canvas_t *image_load_bmp_cached(const char *filepath, int *error_out) {
    /* Check cache first */
    for (int i = 0; i < IMAGE_CACHE_SIZE; i++) {
        if (g_image_cache[i].canvas &&
            strcmp(g_image_cache[i].filepath, filepath) == 0) {
            g_image_cache[i].last_used_tick = get_tick();
            return g_image_cache[i].canvas;
        }
    }

    /* Load from disk */
    gfx_canvas_t *canvas = image_load_bmp(filepath, error_out);
    if (!canvas) return NULL;

    /* Add to cache (evict least recently used) */
    int oldest = 0;
    for (int i = 1; i < IMAGE_CACHE_SIZE; i++) {
        if (g_image_cache[i].last_used_tick < g_image_cache[oldest].last_used_tick) {
            oldest = i;
        }
    }

    if (g_image_cache[oldest].canvas) {
        gfx_free_canvas(g_image_cache[oldest].canvas);
    }

    g_image_cache[oldest].canvas = canvas;
    strncpy(g_image_cache[oldest].filepath, filepath, 255);
    g_image_cache[oldest].filepath[255] = '\0';
    g_image_cache[oldest].last_used_tick = get_tick();

    return canvas;
}
```

---

## Part 7: Testing & Debugging

### Test BMP Creation (Using ImageMagick)

```bash
# Create a simple test image
convert -size 100x100 xc:red test_red.bmp

# Create gradient
convert -size 256x256 gradient:black-white test_gradient.bmp

# Create from PNG
convert photo.png -compress none photo.bmp

# Verify BMP format
file test_red.bmp
hexdump -C test_red.bmp | head -20
```

### Debug Output Example

```c
void debug_print_bmp_info(const char *filepath) {
    void *fd = file_open(filepath);
    if (!fd) {
        printk("[debug] Cannot open %s\n", filepath);
        return;
    }

    bmp_file_header_t file_hdr;
    bmp_dib_header_t dib_hdr;

    file_read(fd, &file_hdr, sizeof(file_hdr));
    file_read(fd, &dib_hdr, sizeof(dib_hdr));

    printk("[BMP] %s\n", filepath);
    printk("  Magic: 0x%04X\n", file_hdr.magic);
    printk("  File size: %u bytes\n", file_hdr.file_size);
    printk("  Pixel offset: %u\n", file_hdr.pixel_offset);
    printk("  Dimensions: %d x %d\n", dib_hdr.width, dib_hdr.height);
    printk("  Bits per pixel: %d\n", dib_hdr.bits_per_pixel);
    printk("  Compression: %d\n", dib_hdr.compression);
    printk("  Colors in palette: %d\n", dib_hdr.colors_used);

    file_close(fd);
}
```

### Memory Verify

```c
void debug_verify_image_load(const char *filepath) {
    u32 mem_before = get_free_memory();
    
    int error;
    gfx_canvas_t *img = image_load_bmp(filepath, &error);
    
    u32 mem_after_load = get_free_memory();
    u32 mem_used_bmp = mem_before - mem_after_load;
    
    printk("[debug] Loaded %s\n", filepath);
    printk("  Canvas: %u x %u\n", img->width, img->height);
    printk("  Expected size: %u bytes\n", img->width * img->height * 4);
    printk("  Memory used: %u bytes\n", mem_used_bmp);
    
    gfx_free_canvas(img);
    
    u32 mem_final = get_free_memory();
    u32 mem_freed = mem_final - mem_after_load;
    
    printk("  Memory freed: %u bytes\n", mem_freed);
    printk("  Memory before: %u, after: %u\n", mem_before, mem_final);
}
```

---

## Summary

✅ Complete BMP decoder for 1, 4, 8, 16, 24, 32-bit images  
✅ Temporary RAM allocation and cleanup  
✅ Original files never modified  
✅ Simple Error handling  
✅ VFS-agnostic file interface  
✅ High-level display functions  
✅ Optional caching for performance  
✅ Debugging utilities  

**Next**: Add PNG/JPEG via offline conversion pipeline (scripts/create_bootable.sh)

