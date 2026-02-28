/**
 * Minimal in-kernel configuration backing.
 *
 * This is intentionally tiny for now: we just hard-code some defaults.
 * Later this can be generated from .kconfig or another build-time
 * source produced by the menuconfig tool.
 */

#include "ark/types.h"
#include "ark/config.h"

bool kcfg_get_bool(kcfg_key_t key) {
    switch (key) {
    case KCFG_FB_ENABLED:
        return CONFIG_FB_ENABLE;
    case KCFG_PRINTK_ENABLED:
        return CONFIG_PRINTK_ENABLE;
    case KCFG_USB_ENABLED:
        return CONFIG_USB_ENABLE;
    case KCFG_AUDIO_ENABLED:
        return CONFIG_AUDIO_ENABLE;
    case KCFG_NET_ENABLED:
        return CONFIG_NET_ENABLE;
    case KCFG_NET_E100:
    case KCFG_NET_E1000:
    case KCFG_NET_RTL8139:
        return CONFIG_NET_ENABLE;
    case KCFG_KBD_100KEY:
        return true;
    case KCFG_DEBUG_VERBOSE:
        return CONFIG_DEBUG_VERBOSE;
    default:
        return false;
    }
}

