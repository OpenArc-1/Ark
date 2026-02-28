/**
 * clear.c - screen clear using VESA framebuffer.
 * VGA text mode (0xB8000) is no longer used.
 */
#include "gpu/vesa.h"

void clear_screen(void) {
    vesa_clear_screen(0x000000);
}
