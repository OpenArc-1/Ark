/**
 * script.h - Kernel Script Scanner Header
 *
 * Provides functions to scan ramfs for #! tagged scripts and execute them.
 */

#pragma once

#include "ark/types.h"

/**
 * Scan ramfs for #!init scripts and execute them
 * 
 * Looks for scripts starting with #!init tag and file:/ directives.
 * Uses ark/printk, input, and hid/kbd100 for execution.
 * 
 * @return 1 if a script was found and executed, 0 otherwise
 */
u8 script_scan_and_execute(void);
