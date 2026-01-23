/**
 * x86_64 architecture entry and early boot glue.
 *
 * A real kernel would enter here from assembly after switching to
 * long mode. For now we model only the C-level entry so that the
 * rest of the kernel code has a clear starting point.
 */

#include "ark/types.h"
#include "ark/fb.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/config.h"

/* Provided by the core kernel. */
void kernel_main(void);

/* Stub: architecture-specific code is expected to discover a linear
 * framebuffer. For now we just expose a weak hook that could be
 * filled in by bootloader/firmware specific code later.
 */
__attribute__((weak))
void arch_x86_64_discover_framebuffer(ark_fb_info_t *out) {
    (void)out;
}

static void arch_early_init(void) {
    /* Discover and initialise framebuffer if enabled. */
    if (kcfg_get_bool(KCFG_FB_ENABLED)) {
        ark_fb_info_t info;
        info.addr = NULL;
        info.pitch = 0;
        info.width = 0;
        info.height = 0;
        info.bpp = 32;

        arch_x86_64_discover_framebuffer(&info);
        if (info.addr) {
            extern void ark_fb_setup(const ark_fb_info_t *info);
            ark_fb_setup(&info);
        }
    }
}

void arch_x86_64_entry(void) {
    arch_early_init();

    printk("Ark kernel (x86_64) starting...\n");

    kernel_main();

    /* kernel_main should never return; if it does, panic. */
    kernel_panic("kernel_main returned");
}

