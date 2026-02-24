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
