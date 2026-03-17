/**
 * printk.h - Ark kernel logging / debug output
 *
 * Supports both VGA and serial (-nographic) output.
 * Includes -like boot logging with timestamps.
 */

#pragma once

#include "ark/types.h"

/* Forward declare phys_addr_t - defined in ark/arch.h */
/* For 32-bit: phys_addr_t = u32, 64-bit: phys_addr_t = u64 */

/* Kernel printk function */
int printk(const char *fmt, ...);

/* ── Enhanced Address Printing API ────────────────────────────────────────── */
/* Print pointer with full address width (auto-detects 32/64-bit) */
void printk_put_ptr(void *ptr);

/* Print physical address with full width */
void printk_put_phys(u64 phys);

/* Print single byte as two hex digits */
void printk_put_hex(u8 byte);

/* Print hex dump of memory region
 * name: label for the dump
 * addr: starting virtual address
 * len: number of bytes to dump */
void printk_hex_dump(const char *name, const void *addr, u32 len);

/* -style kernel logging with automatic timestamps */
 int printk_info(const char *fmt, ...);  /* [    T.XXXXXX] message */
 int printk_warn(const char *fmt, ...);  /* [    T.XXXXXX] [WARN] message (yellow) */
 int printk_err(const char *fmt, ...);   /* [    T.XXXXXX] [ERR] message (red) */
 int printk_ok(const char *fmt, ...);    /* [    T.XXXXXX] [OK] message (green) */
 
 /* System clock (Jiffies-based) */
 u64 jiffies(void);                      /* Get current system ticks */
 u32 printk_tick(void);                  /* Advance system clock by 1 tick */
 void printk_set_hz(u32 hz);             /* Set tick frequency (default 100 Hz) */
 
 /* Global flag: true = use serial (COM1), false = VGA */
 extern bool use_serial;
 
 /* Optional: initialize serial port, returns 1 if available, 0 if not */
 int serial_init(void);
 
 /* Serial input functions */
 bool serial_has_input(void);
 u8 serial_getc(void);
 int printc(u8 color,const char*fmt, ...); 

extern const char *_PRINTK_T_SENTINEL;
#define T _PRINTK_T_SENTINEL

void tsc_calibrate(void);
void tsc_set_hz(u32 ticks_per_us);
u32  tsc_get_mhz(void);   /* CPU MHz measured by tsc_calibrate */

/* Framebuffer text mode setup (call after vesa_init) */
void printk_set_fb(u32 *addr, u32 width, u32 height, u32 pitch, u32 bpp);
void printk_set_graphics_mode(bool enabled);
int  printc_rgb(u32 fg_color, const char *fmt, ...);

/* Cursor control functions */
void printk_cursor_enable(bool enable);         /* Show/hide cursor */
void printk_cursor_set_color(u32 color);       /* Set cursor color (0xRRGGBB) */
void printk_cursor_toggle(void);                /* Toggle cursor visibility */
void printk_cursor_move(int dx, int dy);        /* Move cursor (with bounds) */
void printk_cursor_auto_update(void);           /* Auto-update blinking (call regularly) */
void printk_cursor_update(void);                /* Deprecated: use printk_cursor_auto_update */