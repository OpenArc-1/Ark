/**
 * gpu/efi_gop.c — Ark Kernel UEFI GOP driver
 *
 * Reads the UEFI Graphics Output Protocol framebuffer configured by firmware.
 * Safe to use after ExitBootServices(). Falls back to VBE/BGA if not available.
 *
 * Requires boot.S to expose efi_system_table_ptr:
 *   .globl efi_system_table_ptr
 *   efi_system_table_ptr: .quad 0
 *   # In UEFI entry, before jumping to kernel:
 *   movq %rcx, efi_system_table_ptr(%rip)  # x86_64: EFI passes ST in RCX
 */

#include "ark/types.h"
#include "gpu/efi_gop.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── EFI Type Definitions ────────────────────────────────────────────── */
typedef struct {
    uint32_t d1;
    uint16_t d2, d3;
    uint8_t  d4[8];
} __attribute__((packed)) efi_guid_t;

typedef struct {
    uint64_t sig;
    uint32_t rev;
    uint32_t hdr_sz;
    uint32_t crc;
    uint32_t reserved;
} __attribute__((packed)) efi_table_hdr_t;

typedef struct {
    efi_table_hdr_t      hdr;
    void                *fw_vendor;
    uint32_t             fw_rev;
    void                *con_in_handle;
    void                *con_in;
    void                *con_out_handle;
    void                *con_out;
    void                *stderr_handle;
    void                *stderr_iface;
    void                *runtime_services;
    void                *boot_services;
    size_t               n_cfg;
    struct {
        efi_guid_t guid;
        void      *table;
    } *cfg_table;
} __attribute__((packed)) efi_system_table_t;

/* GOP Mode Info */
typedef struct {
    uint32_t version;
    uint32_t horiz_res;
    uint32_t vert_res;
    uint32_t pixel_fmt;        /* 0=RGBX, 1=BGRX, 2=bitmask, 3=blt-only */
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
    uint32_t pixels_per_scan;  /* stride in pixels */
} __attribute__((packed)) efi_gop_mode_info_t;

/* GOP Mode */
typedef struct {
    uint32_t version;
    uint32_t max_mode;
    uint32_t mode;
    efi_gop_mode_info_t *info;
    size_t info_size;
    uint64_t fb_base;       /* physical framebuffer address */
    size_t fb_size;
} __attribute__((packed)) efi_gop_mode_t;

/* GOP Protocol */
typedef struct {
    void           *query_mode;
    void           *set_mode;
    void           *blt;
    efi_gop_mode_t *mode;
} __attribute__((packed)) efi_gop_t;

/* ── Constants ─────────────────────────────────────────────────────── */
#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ULL

static const efi_guid_t GOP_GUID = {
    0x9042A9DEu, 0x23DC, 0x4A38,
    { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A }
};

/* ── Symbol from boot.S ───────────────────────────────────────────── */
extern uint64_t efi_system_table_ptr __attribute__((weak));

/* ── Helpers ───────────────────────────────────────────────────────── */
static bool guid_eq(const efi_guid_t *a, const efi_guid_t *b) {
    if (a->d1 != b->d1 || a->d2 != b->d2 || a->d3 != b->d3)
        return false;
    for (int i = 0; i < 8; i++)
        if (a->d4[i] != b->d4[i])
            return false;
    return true;
}

/* ── Public API ────────────────────────────────────────────────────── */
bool efi_gop_init(efi_gop_info_t *out) {
    if (!out)
        return false;

    /* Check if EFI system table exists */
    uint64_t st_phys = efi_system_table_ptr;
    if (!st_phys)
        return false;

    efi_system_table_t *st = (efi_system_table_t *)(uintptr_t)st_phys;

    /* Validate EFI System Table signature */
    if (st->hdr.sig != EFI_SYSTEM_TABLE_SIGNATURE)
        return false;

    /* Walk configuration table looking for GOP */
    if (!st->cfg_table || !st->n_cfg)
        return false;

    for (size_t i = 0; i < st->n_cfg; i++) {
        if (!guid_eq(&st->cfg_table[i].guid, &GOP_GUID))
            continue;

        efi_gop_t *gop = (efi_gop_t *)st->cfg_table[i].table;
        if (!gop || !gop->mode || !gop->mode->info)
            return false;

        efi_gop_mode_t *mode = gop->mode;
        efi_gop_mode_info_t *info = mode->info;

        if (!mode->fb_base || !info->horiz_res || !info->vert_res)
            return false;

        /* Compute pitch in bytes */
        uint32_t pps = info->pixels_per_scan ? info->pixels_per_scan : info->horiz_res;
        uint32_t pitch = pps * 4; /* assume 32bpp RGBX/BGRX */

        /* Fill output structure */
        out->framebuffer_base = (uintptr_t)mode->fb_base;
        out->width            = info->horiz_res;
        out->height           = info->vert_res;
        out->pitch            = pitch;
        out->bpp              = 32;
        out->pixel_fmt        = info->pixel_fmt;

        return true;
    }

    return false;  /* GOP not found */
}