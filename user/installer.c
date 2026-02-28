/* simple graphical installer using VESA and PS/2 mouse */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ark/init_api.h"
#include "ark/vesa.h"
#include "ark/mouse.h"

int main(const ark_kernel_api_t *api) {
    ark_fb_t fb;

    /* framebuffer init uses syscall or vesa mapping */
    if (!ark_fb_init(&fb)) {
        if (api && api->printk)
            api->printk("installer: framebuffer unavailable\n");
        return 1;
    }

    ark_mouse_init(fb.width, fb.height);
    ark_mouse_set_pos(fb.width/2, fb.height/2);

    ark_fb_clear(&fb, 0x1E2A5A);
    ark_fb_draw_text(&fb, 100, 100, "ArkOS Installer", 0xFFFFFF, 0x1E2A5A);
    ark_fb_draw_text(&fb, 100, 120, "Click left button to exit", 0xFFFFFF, 0x1E2A5A);

    while (1) {
        ark_mouse_event_t ev;
        if (ark_mouse_poll(&ev)) {
            if (ev.buttons & 1) break;
        }
    }

    return 0;
}
