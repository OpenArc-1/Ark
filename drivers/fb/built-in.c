/**
 * Framebuffer built-in registration for Ark.
 *
 * Architecture-specific code should call ark_fb_setup() with the
 * detected framebuffer mode information.
 */

#include "ark/fb.h"

void ark_fb_setup(const ark_fb_info_t *info) {
    fb_init(info);
}

