/**
 * Simple framebuffer interface used by printk and early boot.
 *
 * The initial implementation targets a linear framebuffer (e.g. VESA/UEFI GOP)
 * and provides a text console API on top of it.
 */

#pragma once

#include "ark/types.h"

typedef struct ark_fb_info {
    u8  *addr;      /* linear framebuffer virtual address */
    u32  pitch;     /* bytes per scanline */
    u32  width;     /* pixels */
    u32  height;    /* pixels */
    u32  bpp;       /* bits per pixel */
} ark_fb_info_t;

/**
 * Initialise framebuffer console with mode information.
 */
void fb_init(const ark_fb_info_t *info);

/**
 * Output a single character to the console.
 * Handles newlines and simple scrolling.
 */
void fb_putc(char c);

/**
 * Clear framebuffer console.
 */
void fb_clear(void);

