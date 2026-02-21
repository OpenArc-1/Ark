/**
 * Ark kernel: printk.c
 * Unified VGA, Serial (COM1), and Formatting API
 *
 * GRAPHICS MODE: When vesa_graphics_active is true, text is rendered
 * to the VESA framebuffer using the 8x16 bitmap font from font.h.
 * VGA text mode writes are disabled to prevent corrupting the framebuffer.
 *
 * TIMESTAMP: Use printk(T, "msg\n") to get [  4.125000] style prefix.
 *            Call tsc_calibrate() once early in boot for accuracy.
 */

#include <stdarg.h>
#include <stdbool.h>
#include "ark/types.h"
#include "ark/printk.h"
#include "font.h"

/* ========================================================= */
/* =================== GRAPHICS MODE FLAG =================== */
/* ========================================================= */

static bool vesa_graphics_active = false;

// Framebuffer pointer, dimensions, and pitch — set these via printk_set_fb()
static u32 *fb_addr   = 0;
static u32  fb_width  = 0;
static u32  fb_height = 0;
static u32  fb_pitch  = 0;   // bytes per row

// Current text cursor position in the framebuffer (in character cells)
static u32 fb_col     = 0;
static u32 fb_row     = 0;
static u32 fb_fg      = 0x00FFFFFF;  // default: white
static u32 fb_bg      = 0x00000000;  // default: black

#define FONT_W   8
#define FONT_H   16

void printk_set_graphics_mode(bool enabled) {
    vesa_graphics_active = enabled;
}

/**
 * Call this after vesa_init to hand the framebuffer info to printk.
 * pitch is in bytes.
 */
void printk_set_fb(u32 *addr, u32 width, u32 height, u32 pitch) {
    fb_addr   = addr;
    fb_width  = width;
    fb_height = height;
    fb_pitch  = pitch;
    fb_col    = 0;
    fb_row    = 0;
}

/* ========================================================= */
/* ===================== SERIAL (COM1) ====================== */
/* ========================================================= */

#define COM1 0x3F8

static inline void outb(u16 port, u8 val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

bool serial_has_input(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

u8 serial_getc(void) {
    if (!serial_has_input()) return 0;
    return inb(COM1);
}

static inline bool is_transmit_empty(void) {
    return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putc(char c) {
    if (c == '\n') {
        while (!is_transmit_empty());
        outb(COM1, '\r');
    }
    while (!is_transmit_empty());
    outb(COM1, c);
}

/* ========================================================= */
/* ======================= VGA TEXT ========================= */
/* ========================================================= */

#define VGA_WIDTH    80
#define VGA_HEIGHT   25
#define DEFAULT_ATTR 0x0F

static volatile u16 *const vga_buf = (volatile u16 *)0xB8000;
static u32 vga_row = 0;
static u32 vga_col = 0;
static u8  current_vga_attr = DEFAULT_ATTR;

static void vga_hw_cursor_update(void) {
    u16 pos = vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 0x0F); outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    for (u32 r = 1; r < VGA_HEIGHT; ++r)
        for (u32 c = 0; c < VGA_WIDTH; ++c)
            vga_buf[(r - 1) * VGA_WIDTH + c] = vga_buf[r * VGA_WIDTH + c];
    for (u32 c = 0; c < VGA_WIDTH; ++c)
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = ((u16)current_vga_attr << 8) | ' ';
    if (vga_row > 0) vga_row--;
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0; vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga_buf[vga_row * VGA_WIDTH + vga_col] = ((u16)current_vga_attr << 8) | ' ';
        }
    } else {
        vga_buf[vga_row * VGA_WIDTH + vga_col] = ((u16)current_vga_attr << 8) | (u8)c;
        vga_col++;
    }
    if (vga_col >= VGA_WIDTH)  { vga_col = 0; vga_row++; }
    if (vga_row >= VGA_HEIGHT) vga_scroll();
    vga_hw_cursor_update();
}

/* ========================================================= */
/* =================== FRAMEBUFFER TEXT ===================== */
/* ========================================================= */

static u32 fb_cols(void) { return fb_width  / FONT_W; }
static u32 fb_rows(void) { return fb_height / FONT_H; }

/**
 * Scroll the framebuffer up by one character row.
 * Moves all rows up by FONT_H pixels, then clears the bottom row.
 */
static void fb_scroll(void) {
    u32 row_bytes = fb_pitch * FONT_H;
    u8 *base = (u8 *)fb_addr;

    // Blit rows 1..N-1 up to rows 0..N-2
    for (u32 y = 0; y < fb_height - FONT_H; y++) {
        u32 *src = (u32 *)(base + (y + FONT_H) * fb_pitch);
        u32 *dst = (u32 *)(base + y * fb_pitch);
        for (u32 x = 0; x < fb_width; x++)
            dst[x] = src[x];
    }
    // Clear the bottom character row
    for (u32 y = fb_height - FONT_H; y < fb_height; y++) {
        u32 *row = (u32 *)(base + y * fb_pitch);
        for (u32 x = 0; x < fb_width; x++)
            row[x] = fb_bg;
    }
    (void)row_bytes;
    if (fb_row > 0) fb_row--;
}

/**
 * Draw one character glyph at character cell (col, row) in the framebuffer.
 */
static void fb_draw_glyph(u32 col, u32 row, char c) {
    if (!fb_addr) return;
    if ((u8)c < 32 || (u8)c > 127) c = '?';

    const u8 *glyph = font8x16[(u8)c - 32];
    u32 px = col * FONT_W;
    u32 py = row * FONT_H;
    u8 *base = (u8 *)fb_addr;

    for (u32 gy = 0; gy < FONT_H; gy++) {
        u32 *line = (u32 *)(base + (py + gy) * fb_pitch);
        u8   bits = glyph[gy];
        for (u32 gx = 0; gx < FONT_W; gx++) {
            line[px + gx] = (bits >> (7 - gx)) & 1 ? fb_fg : fb_bg;
        }
    }
}

static void fb_putc(char c) {
    if (!fb_addr) return;

    if (c == '\n') {
        fb_col = 0;
        fb_row++;
    } else if (c == '\r') {
        fb_col = 0;
    } else if (c == '\b') {
        if (fb_col > 0) {
            fb_col--;
            fb_draw_glyph(fb_col, fb_row, ' ');
        }
    } else {
        fb_draw_glyph(fb_col, fb_row, c);
        fb_col++;
    }

    if (fb_col >= fb_cols()) { fb_col = 0; fb_row++; }
    if (fb_row >= fb_rows()) fb_scroll();
}

/* ========================================================= */
/* ====================== TIMESTAMP ========================= */
/* ========================================================= */

// Magic sentinel pointer — printk checks for this to detect T usage
const char *_PRINTK_T_SENTINEL = (const char *)0x1;

// TSC ticks per microsecond (32-bit). Avoids 64-bit division (__udivdi3) entirely.
// Default: 1000 ticks/us = 1 GHz. tsc_calibrate() sets this accurately.
static u32 tsc_hz_per_us = 1000;

static inline u64 rdtsc(void) {
    u32 lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/**
 * tsc_calibrate — measure TSC ticks/us using PIT channel 2 as a ~50ms reference.
 * Call once early in boot. No libgcc, no 64-bit division.
 */
void tsc_calibrate(void) {
    u16 divisor = 59659;  // PIT divisor for ~50ms (1193182 Hz / 20)

    // Enable PIT channel 2 gate, disable speaker
    u8 ctrl = inb(0x61);
    ctrl &= ~0x02;
    ctrl |=  0x01;
    outb(0x61, ctrl);

    // Program PIT channel 2: lobyte/hibyte, oneshot
    outb(0x43, 0xB0);
    outb(0x42, (u8)(divisor & 0xFF));
    outb(0x42, (u8)((divisor >> 8) & 0xFF));

    u64 start = rdtsc();
    while (inb(0x61) & 0x20);   // wait ~50ms for counter to expire
    u64 end = rdtsc();

    // ticks in 50ms. Multiply by 20 to get ticks/sec, divide by 1,000,000 for ticks/us.
    // All done in 32-bit: delta fits in 32 bits for CPUs up to ~214 GHz at 50ms.
    u32 delta = (u32)(end - start);  // ticks in 50ms — safe: 50ms * 4GHz = 200M < 2^32
    tsc_hz_per_us = (delta * 20) / 1000000;  // pure 32-bit math
    if (tsc_hz_per_us == 0) tsc_hz_per_us = 1; // guard for very slow/emulated CPUs

    outb(0x61, ctrl & ~0x01);
}

/**
 * Manually set CPU speed. Pass ticks per microsecond.
 * e.g. tsc_set_hz(3600) for a 3.6 GHz CPU.
 */
void tsc_set_hz(u32 ticks_per_us) {
    tsc_hz_per_us = ticks_per_us;
}

/* ========================================================= */
/* ==================== FORMATTING LOGIC ==================== */
/* ========================================================= */

static void putc_internal(char c) {
    if (vesa_graphics_active)
        fb_putc(c);
    else
        vga_putc(c);
    serial_putc(c);     // Always outputs to serial
}

static void print_string(const char *s) {
    if (!s) s = "(null)";
    while (*s) putc_internal(*s++);
}

static void print_uint(u32 v, u32 base, bool uppercase) {
    char buf[32];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) { putc_internal('0'); return; }
    while (v) { buf[i++] = digits[v % base]; v /= base; }
    while (i--) putc_internal(buf[i]);
}

static void print_int(int v) {
    if (v < 0) { putc_internal('-'); print_uint((u32)(-v), 10, false); }
    else        print_uint((u32)v, 10, false);
}

/**
 * Print a [SSSS.MMMMMM] timestamp — pure 32-bit math, zero libgcc helpers.
 *
 * Tracks elapsed microseconds by accumulating TSC deltas between calls.
 * Uses only the low 32 bits of RDTSC — unsigned subtraction handles wrapping
 * correctly, so the accumulator is always monotonic.
 */
static u32 ts_last_lo  = 0;
static u32 ts_us_accum = 0;

static void print_timestamp(void) {
    u32 lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo) : : "edx");

    // Unsigned subtraction wraps correctly even when lo overflows 32 bits
    u32 delta = lo - ts_last_lo;
    ts_last_lo = lo;

    // Pure u32 divide: ticks_per_us is at most ~4000, delta at most ~4B — no libgcc
    ts_us_accum += delta / tsc_hz_per_us;

    u32 sec = ts_us_accum / 1000000;
    u32 us  = ts_us_accum % 1000000;

    putc_internal('[');

    // Seconds — right-aligned in 4 chars with spaces
    if (sec < 1000) putc_internal(' ');
    if (sec < 100)  putc_internal(' ');
    if (sec < 10)   putc_internal(' ');
    print_uint(sec, 10, false);

    putc_internal('.');

    // Microseconds — left-padded with zeros to 6 digits
    if (us < 100000) putc_internal('0');
    if (us < 10000)  putc_internal('0');
    if (us < 1000)   putc_internal('0');
    if (us < 100)    putc_internal('0');
    if (us < 10)     putc_internal('0');
    print_uint(us, 10, false);

    putc_internal(']');
    putc_internal(' ');
}

/* ========================================================= */
/* ======================= PUBLIC API ======================= */
/* ========================================================= */

int vprintk(const char *fmt, va_list ap) {
    int count = 0;
    while (*fmt) {
        if (*fmt != '%') { putc_internal(*fmt++); count++; continue; }
        fmt++;
        switch (*fmt++) {
            case '%': putc_internal('%'); count++; break;
            case 'c': putc_internal((char)va_arg(ap, int)); count++; break;
            case 's': print_string(va_arg(ap, char*)); break;
            case 'd':
            case 'i': print_int(va_arg(ap, int)); break;
            case 'u': print_uint(va_arg(ap, u32), 10, false); break;
            case 'x': print_uint(va_arg(ap, u32), 16, false); break;
            case 'X': print_uint(va_arg(ap, u32), 16, true);  break;
            default: break;
        }
    }
    return count;
}

/**
 * printk — standard kernel print.
 *
 * Usage:
 *   printk("hello\n");               →  hello
 *   printk(T, "hello\n");            →  [   4.125000] hello
 *   printk(T, "val = %d\n", 42);     →  [   4.125001] val = 42
 *
 * T is a magic sentinel defined in printk.h. When detected as the first
 * argument, the real format string is read from the next va_arg, and a
 * timestamp is prepended automatically.
 */
int printk(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int n;
    if (fmt == _PRINTK_T_SENTINEL) {
        // T mode: next arg is the actual format string
        const char *real_fmt = va_arg(ap, const char *);
        print_timestamp();
        n = vprintk(real_fmt, ap);
    } else {
        n = vprintk(fmt, ap);
    }

    va_end(ap);
    return n;
}

int printc(u8 color, const char *fmt, ...) {
    // In graphics mode, interpret 'color' as a packed 0xRRGGBB fg value.
    // In VGA text mode, use it as the standard VGA attribute byte.
    u32 old_fb_fg = fb_fg;
    u8  old_vga   = current_vga_attr;

    if (vesa_graphics_active)
        fb_fg = (u32)color;   // caller should pass a 32-bit RGB — see note below
    else
        current_vga_attr = color;

    va_list ap;
    va_start(ap, fmt);
    int n = vprintk(fmt, ap);
    va_end(ap);

    fb_fg = old_fb_fg;
    current_vga_attr = old_vga;
    return n;
}

/**
 * printc_rgb: graphics-mode aware color printing.
 * Use this when in VESA mode to pass a full 0x00RRGGBB color.
 */
int printc_rgb(u32 fg_color, const char *fmt, ...) {
    u32 old = fb_fg;
    fb_fg = fg_color;

    va_list ap;
    va_start(ap, fmt);
    int n = vprintk(fmt, ap);
    va_end(ap);

    fb_fg = old;
    return n;
}