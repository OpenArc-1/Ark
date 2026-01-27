/**
 * printk.h - Ark kernel logging / debug output
 *
 * Supports both VGA and serial (-nographic) output.
 */

 #pragma once

 #include "ark/types.h"
<<<<<<< HEAD
=======
 #include <stdarg.h>
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
 #include <stdbool.h>
 
 /* Kernel printk function */
 int printk(const char *fmt, ...);
<<<<<<< HEAD
=======

 /* Varargs variant for wrappers (e.g. tty_debug) */
 int vprintk(const char *fmt, va_list ap);
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
 
 /* Global flag: true = use serial (COM1), false = VGA */
 extern bool use_serial;
 
 /* Optional: initialize serial port */
 void serial_init(void);
 
 /* Serial input functions */
 bool serial_has_input(void);
 u8 serial_getc(void);
 