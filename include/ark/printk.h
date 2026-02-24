/**
 * printk.h - Ark kernel logging / debug output
 *
 * Supports both VGA and serial (-nographic) output.
 */

 #pragma once

 #include "ark/types.h"
 
 /* Kernel printk function */
 int printk(const char *fmt, ...);
 
 /* Global flag: true = use serial (COM1), false = VGA */
 extern bool use_serial;
 
 /* Optional: initialize serial port */
 void serial_init(void);
 
 /* Serial input functions */
 bool serial_has_input(void);
 u8 serial_getc(void);
 int printc(u8 color,const char*fmt, ...); 

extern const char *_PRINTK_T_SENTINEL;
#define T _PRINTK_T_SENTINEL

void tsc_calibrate(void);
void tsc_set_hz(u32 ticks_per_us);

/* Framebuffer text mode setup (call after vesa_init) */
void printk_set_fb(u32 *addr, u32 width, u32 height, u32 pitch);
void printk_set_graphics_mode(bool enabled);
int  printc_rgb(u32 fg_color, const char *fmt, ...);

/* Cursor control functions */
void printk_cursor_enable(bool enable);         /* Show/hide cursor */
void printk_cursor_set_color(u32 color);       /* Set cursor color (0xRRGGBB) */
void printk_cursor_toggle(void);                /* Toggle cursor visibility */
void printk_cursor_move(int dx, int dy);        /* Move cursor (with bounds) */
void printk_cursor_auto_update(void);           /* Auto-update blinking (call regularly) */
void printk_cursor_update(void);                /* Deprecated: use printk_cursor_auto_update */