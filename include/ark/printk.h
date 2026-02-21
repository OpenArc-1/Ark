/**
 * printk.h - Ark kernel logging / debug output
 *
 * Supports both VGA and serial (-nographic) output.
 */

 #pragma once

 #include "ark/types.h"
 #include <stdbool.h>
 
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