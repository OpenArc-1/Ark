/**
 * gpu/efi_gop.c  —  Ark Kernel  UEFI GOP driver
 *
 * Reads the UEFI GOP Mode structure that the firmware set up before boot.
 * Never calls Boot Services — safe after ExitBootServices().
 *
 * To enable: make menuconfig → Framebuffer / GPU → FB driver → efi_gop
 *
 * boot.S must save the EFI System Table pointer before entering C:
 *   .globl efi_system_table_ptr
 *   efi_system_table_ptr: .quad 0
 *   # in .Lentry64, before call arch_x86_64_entry:
 *   movq %rbx, efi_system_table_ptr(%rip)
 */

#include "ark/types.h"
#include "gpu/efi_gop.h"

#if defined(CONFIG_FB_DRIVER_EFI_GOP) && CONFIG_FB_DRIVER_EFI_GOP

typedef struct { u32 d1; u16 d2, d3; u8 d4[8]; } __attribute__((packed)) efi_guid_t;
typedef struct { u64 sig; u32 rev, hdr_sz, crc, res; } __attribute__((packed)) efi_hdr_t;
typedef struct { efi_guid_t guid; void *table; } __attribute__((packed)) efi_cfg_t;
typedef struct {
    efi_hdr_t hdr;
    u16 *fw_vendor; u32 fw_rev; u32 _pad;
    void *cin_h, *cin, *cout_h, *cout, *cerr_h, *cerr;
    void *rt, *bs;
    usize n_cfg;
    efi_cfg_t *cfg;
} __attribute__((packed)) efi_st_t;

typedef struct {
    u32 ver, xres, yres, fmt;
    u32 rm, gm, bm, rsv, ppsl;
} __attribute__((packed)) efi_gop_mi_t;

typedef struct {
    u32 max_mode, mode;
    efi_gop_mi_t *info;
    usize info_sz;
    u64 fb_base;
    usize fb_sz;
} __attribute__((packed)) efi_gop_mode_t;

typedef struct { void *qm, *sm, *blt; efi_gop_mode_t *mode; } __attribute__((packed)) efi_gop_t;

#define EFI_ST_SIG 0x5453595320494249ULL
static const efi_guid_t GOP_GUID = { 0x9042A9DEu, 0x23DC, 0x4A38,
    {0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A} };

extern u64 efi_system_table_ptr __attribute__((weak));

static int guid_eq(const efi_guid_t *a, const efi_guid_t *b) {
    if (a->d1!=b->d1||a->d2!=b->d2||a->d3!=b->d3) return 0;
    for (int i=0;i<8;i++) if (a->d4[i]!=b->d4[i]) return 0;
    return 1;
}

bool efi_gop_init(efi_gop_info_t *out) {
    if (!out) return false;
    u64 p = (&efi_system_table_ptr) ? efi_system_table_ptr : 0ULL;
    if (!p) return false;
    efi_st_t *st = (efi_st_t *)(usize)p;
    if (st->hdr.sig != EFI_ST_SIG) return false;
    for (usize i = 0; i < st->n_cfg; i++) {
        if (guid_eq(&st->cfg[i].guid, &GOP_GUID)) {
            efi_gop_t *gop = (efi_gop_t *)st->cfg[i].table;
            if (!gop||!gop->mode||!gop->mode->info||!gop->mode->fb_base) return false;
            u32 pitch = gop->mode->info->ppsl * 4u;
            if (!pitch) pitch = gop->mode->info->xres * 4u;
            out->framebuffer_base = (usize)gop->mode->fb_base;
            out->width  = gop->mode->info->xres;
            out->height = gop->mode->info->yres;
            out->pitch  = pitch;
            out->bpp    = 32;
            return true;
        }
    }
    return false;
}

#else
/* Stub — links cleanly on BIOS builds */
bool efi_gop_init(efi_gop_info_t *out) { (void)out; return false; }
#endif
