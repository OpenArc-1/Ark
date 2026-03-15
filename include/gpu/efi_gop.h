#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "ark/types.h"

typedef struct {
    usize framebuffer_base;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    u32 pixel_fmt;
} efi_gop_info_t;

/* Correct declaration: matches the source file */
bool efi_gop_init(efi_gop_info_t *out);