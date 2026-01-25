/**
* Core kernel boot flow for Ark.
*
 * This is architecture-agnostic high-level logic that is entered
 * from arch-specific code (e.g. arch_x86_64_entry).
 */

 #include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "clear.h"
#include "../usb/usb.h"
#include "../wf/e1000.h"
#include "ark/fb.h"
#include "../fs/ramfs.h"
#include "../fs/ata.h"
#include "../fs/sata.h"

extern void clear_screen(void);
extern void usb_init(void);
extern void e1000_init(void);
extern void fs_built_in_init(void);
extern ark_fb_info_t g_fb_info;  /* Framebuffer info from bootloader */
/* Forward declarations for future subsystems. */
u8 fs_has_init(void);
void fs_mount_root(void);
void input_init(void);  /* Input subsystem manager */
void input_poll(void);  /* Poll input devices */

static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

static void wait_for_init_bin(void) {
    /* Check if init.bin was provided by bootloader as a module.
     * If modules were loaded into ramfs, fs_has_init() will return true immediately.
     */
    if (fs_has_init()) {
        printk("  [    0.100000] ark-init: found /init.bin in ramfs\n");
        return;
    }

    printk("[    0.100000] ark-init: /init.bin not found in ramfs\n");
    printk("[    0.110000] ark-init: waiting for init.bin...\n");
    
    /* Try waiting a bit in case modules are still being loaded */
    for (int attempt = 1; attempt <= 3; ++attempt) {
        printk("[    0.120000] ark-init: retrying (%d/3)...\n", attempt);
        busy_delay(50000000);
        if (fs_has_init()) {
            printk("[    0.150000] ark-init: found /init.bin, starting userspace\n");
            return;
        }
    }

    printk("[    0.200000] ark-init: /init.bin not found after retries\n");
    printk("[    0.210000] ark-init: To load init.bin, run: make run-with-init\n");
    kernel_panic("init.bin not found");
}

void kernel_main(void) {
    fb_init(&g_fb_info);
    serial_init();
    clear_screen();
    busy_delay(20000000);
    printk("[    0.000000] Ark kernel booting on x86\n");
    printk("[    0.000001] Boot params: stub (no cmdline yet)\n");
    
    printk("[    0.000100] Initialising core subsystems...\n");
    /* TODO: initialise memory manager, scheduler, device model, etc. */
    input_init();  /* Initialize input subsystem (keyboard, touch, etc.) */
    busy_delay(20000000);
    usb_init();
    busy_delay(20000000);
    printk("[    0.000150] Probing network device (e1000)...\n");
    e1000_init();
    busy_delay(20000000);
    fs_built_in_init();  /* Initialize filesystem and storage drivers */
    busy_delay(20000000);
    printk("[    0.000200] Mounting root filesystem (ramfs)...\n");
    ramfs_init();
    fs_mount_root();
    busy_delay(20000000);

    printk("[    0.000300] Probing for /init.bin\n");
    wait_for_init_bin();

    /* If fs_has_init ever returns true, we would "launch" init here. */
    printk("[    0.600000] Launching init (stub)...\n");
    printk("[    0.670000] Launching init (blob)...\n");
    
    /* Poll input devices while waiting */
    input_poll();
    
    busy_delay(20000000);
    kernel_panic("Reached end of kernel_main without real init");
}

/* Filesystem implementation using ramfs for loading init.bin */
u8 fs_has_init(void) {
    return ramfs_has_init();
}

void fs_mount_root(void) {
    ramfs_mount();
}

