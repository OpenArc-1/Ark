/**
 * Simple kernel configuration/query interface.
 *
 * Configuration values are expected to be generated from the
 * host-side menuconfig tool (which writes .kconfig).
 *
 * For now we expose only a minimal runtime query API; the backing
 * implementation can later be wired to real build-time or boot-time
 * config.
 */

#pragma once

#include "ark/types.h"

typedef enum {
    KCFG_FB_ENABLED = 0,
    KCFG_PRINTK_ENABLED,
    KCFG_USB_ENABLED,
    KCFG_AUDIO_ENABLED,
    KCFG_NET_ENABLED,
    KCFG_NET_E100,
    KCFG_NET_E1000,
    KCFG_NET_RTL8139,
    KCFG_KBD_100KEY,
    KCFG_DEBUG_VERBOSE,
} kcfg_key_t;

bool kcfg_get_bool(kcfg_key_t key);

