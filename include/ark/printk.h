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
 