/*
 * Sample DKM (Dynamic Kernel Module)
 *
 * This module can be compiled standalone or with full kernel API support.
 *
 * Standalone (minimal): 
 *   ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init -o sample.elf \
 *     ks/dkmload/sample_module.c
 *
 * With kernel API:
 *   ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init -o sample.elf \
 *     -I include ks/dkmload/sample_module.c
 */

#include "ark/types.h"

/* Try to include kernel API, but don't fail if it's not available */
#ifdef __has_include
  #if __has_include("ark/init_api.h")
    #include "ark/init_api.h"
    #define HAVE_KERNEL_API 1
  #endif
#endif

#ifndef HAVE_KERNEL_API
/* Stub for standalone mode */
typedef struct {
    u32 version;
} ark_kernel_api_t;
#endif

/*
 * Module initialization.
 * Can be called with or without kernel API.
 * The signature must match what the loader provides.
 */

#ifdef HAVE_KERNEL_API
int module_init(const ark_kernel_api_t *api) {
    if (api && api->printk)
        api->printk("sample_module: initialized with kernel API\n");
    return 0;
}
#else
int module_init(void) {
    /* Standalone mode - no kernel API available */
    return 0;
}
#endif

/*
 * Optional module exit (not yet called by loader, reserved for future use).
 */
#ifdef HAVE_KERNEL_API
int module_exit(const ark_kernel_api_t *api) {
    if (api && api->printk)
        api->printk("sample_module: cleanup\n");
    return 0;
}
#endif

