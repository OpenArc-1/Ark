/**
 * include/gpu/efi_gop.h  —  Ark Kernel  UEFI GOP interface
 *
 * Only used when CONFIG_FB_DRIVER_EFI_GOP is set via menuconfig.
 * On BIOS systems efi_gop_init() returns false and the caller
 * falls back to BGA automatically.
 */
#pragma once
#include "ark/types.h"

typedef struct {
    usize framebuffer_base;   /* physical address of linear framebuffer */
    u32   width;
    u32   height;
    u32   pitch;              /* bytes per scan-line                     */
    u32   bpp;                /* always 32 for GOP                       */
} efi_gop_info_t;

/**
 * efi_gop_init — locate UEFI GOP and fill *out.
 * Safe after ExitBootServices — never calls Boot Services.
 * Returns true on success, false if GOP unavailable (BIOS boot etc.).
 */
bool efi_gop_init(efi_gop_info_t *out);
