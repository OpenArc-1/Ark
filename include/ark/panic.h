/**
 * Kernel panic handling.
 */

#pragma once

#include "ark/types.h"

/**
 * Print a panic message and halt the CPU.
 *
 * This should never return.
 */
void kernel_panic(const char *msg);

/**
 * Kernel panic with QR code display.
 * Shows a QR code containing debug information.
 */
void kernel_panic_with_qr(const char *msg);

/**
 * Log capture for panic QR code.
 * These functions capture printk output for inclusion in QR code.
 */
void panic_log_putchar(char c);
void panic_log_write(const char *s);
void panic_log_clear(void);
const char *panic_get_log(void);
void panic_set_boot_phase(const char *phase);
const char *panic_get_last_phase(void);

