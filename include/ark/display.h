/**
 * Display Manager API for Ark kernel
 * 
 * Provides unified framebuffer console for 480p output
 */

#pragma once

#include "ark/types.h"
#include "ark/fb.h"

/**
 * Initialize display manager with framebuffer information
 */
void display_init(const ark_fb_info_t *fb_info);

/**
 * Clear the entire display
 */
void display_clear(void);

/**
 * Output a single character to the display
 * Handles scrolling and cursor management
 */
void display_putc(char c);

/**
 * Output a string to the display
 */
void display_puts(const char *s);

/**
 * Get display grid dimensions (in characters)
 */
u32 display_get_width(void);
u32 display_get_height(void);

/**
 * Check if display is initialized
 */
u32 display_is_initialized(void);

/**
 * Get framebuffer address (exported to userspace)
 */
u32 *display_get_framebuffer(void);

/**
 * Prevent VGA screen blanking on real hardware.
 *
 * Clears VGA Sequencer register 1 bit 5 (clock-disable / Screen-Off)
 * which the BIOS or firmware sometimes leaves set, causing the display
 * to go dark ~1 minute after boot. Call this from display_init() and
 * periodically from the scheduler tick if needed.
 *
 * This is the same fix applied by Linux's fbcon driver.
 */
void display_vga_unblank(void);
