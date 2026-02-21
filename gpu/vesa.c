/**
 * vesa.c - VESA VBE 2.0 framebuffer driver for ARK kernel
 * 
 * Sets up 1024x768x32 (RGB888) graphics mode via BIOS interrupt 0x10
 * 
 * Usage:
 *   1. Call vesa_init() early in kernel_main (before paging if possible)
 *   2. Access framebuffer via vesa_get_framebuffer()
 *   3. Draw pixels with vesa_putpixel(x, y, color)
 *   4. Use vesa_fill_rect() for fast fills
 * 
 * QEMU test:
 *   qemu-system-i386 -kernel bzImage -vga std
 */

#include "ark/types.h"
#include "ark/printk.h"

// =============================================================================
// VBE structures passed via multiboot or probed at runtime
// =============================================================================

typedef struct __attribute__((packed)) {
    u16 attributes;
    u8  window_a;
    u8  window_b;
    u16 granularity;
    u16 window_size;
    u16 segment_a;
    u16 segment_b;
    u32 win_func_ptr;
    u16 pitch;
    u16 width;
    u16 height;
    u8  w_char;
    u8  y_char;
    u8  planes;
    u8  bpp;
    u8  banks;
    u8  memory_model;
    u8  bank_size;
    u8  image_pages;
    u8  reserved0;
    u8  red_mask;
    u8  red_position;
    u8  green_mask;
    u8  green_position;
    u8  blue_mask;
    u8  blue_position;
    u8  reserved_mask;
    u8  reserved_position;
    u8  direct_color_attributes;
    u32 framebuffer;
    u32 off_screen_mem_off;
    u16 off_screen_mem_size;
    u8  reserved1[206];
} vbe_mode_info_t;

// =============================================================================
// Driver state
// =============================================================================

static u8    *vesa_fb      = NULL;   // framebuffer linear address
static u32    vesa_width   = 1024;
static u32    vesa_height  = 768;
static u32    vesa_pitch   = 0;      // bytes per scanline
static u32    vesa_bpp     = 32;
static bool   vesa_ready   = false;

// =============================================================================
// BIOS interrupt caller (requires real mode or VM86 — simplified stub here)
// In a real kernel, you'd use a real-mode trampoline or rely on bootloader.
// =============================================================================

/**
 * vesa_set_mode_bios() - Call VBE BIOS function to set video mode
 * 
 * In a proper implementation, this would:
 * 1. Drop to real mode or use VM86
 * 2. Set AX=0x4F02, BX=mode|0x4000 (linear framebuffer bit)
 * 3. INT 0x10
 * 4. Return to protected mode
 * 
 * For simplicity, this stub assumes the bootloader (GRUB) already set the mode
 * and passed the framebuffer address via multiboot info.
 */
static bool vesa_set_mode_bios(u16 mode) {
    // Real implementation would call INT 0x10 here
    // Stub: assume bootloader already set mode
    return true;
}

// =============================================================================
// Initialization
// =============================================================================

/**
 * vesa_init() - Initialize VESA framebuffer
 * 
 * OPTION 1 (preferred): Get framebuffer address from multiboot
 * If your bootloader (GRUB) passed multiboot info with framebuffer fields:
 *   - framebuffer_addr (physical address)
 *   - framebuffer_pitch, width, height, bpp
 * 
 * OPTION 2 (fallback): Probe VBE info block at 0x00000500 or use hardcoded
 * Some bootloaders leave VBE info at a known location. QEMU with -vga std
 * typically sets LFB at 0xE0000000 or 0xFD000000.
 * 
 * @param fb_addr   Physical framebuffer address (from bootloader or probe)
 * @param width     Horizontal resolution
 * @param height    Vertical resolution
 * @param pitch     Bytes per scanline (usually width * (bpp/8))
 * @param bpp       Bits per pixel (typically 32 for RGB888)
 */
void vesa_init(u32 fb_addr, u32 width, u32 height, u32 pitch, u32 bpp) {
    printk("[VESA] Init called: fb=0x");
    // Print address byte by byte since %08x doesn't work
    printk("%02x", (fb_addr >> 24) & 0xFF);
    printk("%02x", (fb_addr >> 16) & 0xFF);
    printk("%02x", (fb_addr >>  8) & 0xFF);
    printk("%02x", (fb_addr >>  0) & 0xFF);
    printk(", %ux%ux%u\n", width, height, bpp);
    
    if (vesa_ready) {
        printk("[VESA] Already initialized\n");
        return;
    }

    // Sanity checks
    if (!fb_addr) {
        printk("[VESA] ERROR: framebuffer address is NULL\n");
        return;
    }
    if (width == 0 || height == 0 || bpp == 0) {
        printk("[VESA] ERROR: invalid dimensions %ux%ux%u\n", width, height, bpp);
        return;
    }

    // Test if framebuffer is writable
    printk("[VESA] Testing write at 0x");
    printk("%02x%02x%02x%02x...\n",
           (fb_addr >> 24) & 0xFF, (fb_addr >> 16) & 0xFF,
           (fb_addr >>  8) & 0xFF, (fb_addr >>  0) & 0xFF);
    
    volatile u32 *test = (volatile u32 *)fb_addr;
    
    // Try to read first
    u32 old_val = *test;
    printk("[VESA] Read: 0x%x\n", old_val);
    
    // Try to write
    *test = 0xDEADBEEF;
    u32 read_back = *test;
    printk("[VESA] Wrote 0xDEADBEEF, read back: 0x%x\n", read_back);
    
    if (read_back != 0xDEADBEEF) {
        printk("[VESA] ERROR: framebuffer not writable!\n");
        printk("[VESA] Address might be wrong or not accessible\n");
        return;
    }
    
    *test = old_val;  // restore
    printk("[VESA] Write test PASSED\n");

    vesa_fb     = (u8 *)fb_addr;
    vesa_width  = width;
    vesa_height = height;
    vesa_pitch  = pitch ? pitch : (width * (bpp / 8));
    vesa_bpp    = bpp;
    vesa_ready  = true;

    printk("[VESA] Initialized: %ux%ux%u, pitch=%u\n",
           vesa_width, vesa_height, vesa_bpp, vesa_pitch);
}

/**
 * vesa_init_default() - Initialize with hardcoded fallback values
 * 
 * Attempts to detect framebuffer address via common QEMU/VirtualBox locations.
 * For QEMU with `-vga std`, the LFB is typically at 0xE0000000 or 0xFD000000.
 * For real hardware, the address varies — best to get it from multiboot.
 */
void vesa_init_default(void) {
    printk("[VESA] Auto-detecting framebuffer address...\n");
    
    // Common QEMU LFB addresses (try in order)
    u32 probe_addrs[] = { 
        0xE0000000,  // QEMU -vga std (most common)
        0xFD000000,  // QEMU alternative
        0xF0000000,  // Older QEMU
        0xFC000000,  // VirtualBox sometimes
        0 
    };

    for (int i = 0; probe_addrs[i] != 0; i++) {
        u32 fb = probe_addrs[i];
        printk("[VESA] Trying 0x%08x... ", fb);
        
        // Test if framebuffer is writable by writing a test pattern
        volatile u32 *test = (volatile u32 *)fb;
        u32 old_val = *test;
        *test = 0xDEADBEEF;
        u32 read_back = *test;
        *test = old_val;  // restore immediately
        
        if (read_back == 0xDEADBEEF) {
            printk("SUCCESS\n");
            vesa_init(fb, 1024, 768, 0, 32);
            return;
        }
        printk("failed (read 0x%08x)\n", read_back);
    }

    printk("[VESA] No framebuffer found at common addresses\n");
    printk("[VESA] Try: vesa_init(addr, 1024, 768, 0, 32) with your actual FB address\n");
}

// =============================================================================
// Drawing primitives
// =============================================================================

/**
 * vesa_putpixel() - Draw a single pixel
 * 
 * @param x      X coordinate (0 = left)
 * @param y      Y coordinate (0 = top)
 * @param color  RGB color in 0xRRGGBB format
 */
void vesa_putpixel(u32 x, u32 y, u32 color) {
    if (!vesa_ready || x >= vesa_width || y >= vesa_height) return;

    u32 offset = y * vesa_pitch + x * (vesa_bpp / 8);
    u32 *pixel = (u32 *)(vesa_fb + offset);
    *pixel = color;
}

/**
 * vesa_fill_rect() - Fill a rectangle with solid color
 * 
 * @param x      Top-left X
 * @param y      Top-left Y
 * @param w      Width
 * @param h      Height
 * @param color  RGB color
 */
void vesa_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!vesa_ready) return;

    for (u32 py = y; py < y + h && py < vesa_height; py++) {
        u32 offset = py * vesa_pitch + x * (vesa_bpp / 8);
        u32 *line = (u32 *)(vesa_fb + offset);
        for (u32 px = 0; px < w && (x + px) < vesa_width; px++) {
            line[px] = color;
        }
    }
}

/**
 * vesa_clear_screen() - Fill entire screen with color
 */
void vesa_clear_screen(u32 color) {
    vesa_fill_rect(0, 0, vesa_width, vesa_height, color);
}

/**
 * vesa_draw_line() - Draw a line using Bresenham's algorithm
 */
void vesa_draw_line(u32 x0, u32 y0, u32 x1, u32 y1, u32 color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
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

/**
 * vesa_draw_char() - Draw an 8x16 VGA font character
 * 
 * Requires a font bitmap. For simplicity, this is a stub.
 * In a real implementation, you'd load the VGA font from 0xFFA6E or
 * embed a PSF font.
 */
void vesa_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg) {
    // TODO: implement with actual font data
    // For now, just draw a placeholder rect
    vesa_fill_rect(x, y, 8, 16, bg);
}

// =============================================================================
// Getters
// =============================================================================

u8 *vesa_get_framebuffer(void) { return vesa_fb; }
u32 vesa_get_width(void)        { return vesa_width; }
u32 vesa_get_height(void)       { return vesa_height; }
u32 vesa_get_pitch(void)        { return vesa_pitch; }
u32 vesa_get_bpp(void)          { return vesa_bpp; }
bool vesa_is_ready(void)        { return vesa_ready; }

// =============================================================================
// Test pattern
// =============================================================================

/**
 * vesa_test_pattern() - Draw a colorful test pattern
 */
void vesa_test_pattern(void) {
    if (!vesa_ready) return;

    // Clear to black first
    vesa_clear_screen(0x000000);
    
    // Try BGR order instead of RGB
    // Top-left: BLUE (was red)
    vesa_fill_rect(0, 0, 256, 256, 0x0000FF);
    
    // Top-right: GREEN
    vesa_fill_rect(768, 0, 256, 256, 0x00FF00);
    
    // Bottom-left: RED (was blue)
    vesa_fill_rect(0, 512, 256, 256, 0xFF0000);
    
    // Center: WHITE
    vesa_fill_rect(384, 256, 256, 256, 0xFFFFFF);
    
    // Yellow box (test mixed colors)
    vesa_fill_rect(200, 200, 100, 100, 0x00FFFF);
    
    // Border
    vesa_draw_line(0, 0, vesa_width-1, 0, 0xFFFFFF);
    vesa_draw_line(vesa_width-1, 0, vesa_width-1, vesa_height-1, 0xFFFFFF);
    vesa_draw_line(vesa_width-1, vesa_height-1, 0, vesa_height-1, 0xFFFFFF);
    vesa_draw_line(0, vesa_height-1, 0, 0, 0xFFFFFF);

    printk("[VESA] Test pattern drawn\n");
}