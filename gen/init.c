/**
* Core kernel boot flow for Ark.
*
 * This is architecture-agnostic high-level logic that is entered
 * from arch-specific code (e.g. arch_x86_64_entry).
 */

 #include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/clear.h"
#include "ark/usb.h"
#include "ark/e1000.h"
#include "ark/ip.h"
#include "ark/fb.h"
#include "ark/ramfs.h"
#include "ark/ata.h"
#include "ark/sata.h"
#include "../mp/built-in.h"

extern void show_sysinfo_bios(void);
extern void clear_screen(void);
extern void usb_init(void);
extern void e1000_init(void);
extern void ip_init(void);
extern void ip_poll(void);
extern void fs_built_in_init(void);
extern void fb_init(const ark_fb_info_t *info);
extern void serial_init(void);
extern void ramfs_init(void);
extern void ramfs_mount(void);
extern u8 ramfs_has_init(void);
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
    printk("\n");
    printk("[    0.000000] ========================================\n");
    printk("[    0.000000] Ark kernel booting on x86\n");
    printk("[    0.000000] ========================================\n");
    printk("[    0.000001] Boot params: stub (no cmdline yet)\n");
    printk("[    0.000010] Framebuffer: initialized\n");
    printk("[    0.000020] Serial console: initialized\n");
    
    printk("[    0.000100] Initialising core subsystems...\n");
    /* Input subsystem */
    printk("[    0.000110] Initializing input subsystem...\n");
    input_init();
    printk("[    0.000120] Input subsystem: OK\n");
    busy_delay(20000000);
    /*bios subsystem*/
    printk("[    0.000125] Initializing BIOS subsystem...\n");
    show_sysinfo_bios();
    busy_delay(20000000);
    /* USB subsystem */
    printk("[    0.000130] Initializing USB subsystem...\n");
    usb_init();
    printk("[    0.000140] USB subsystem: OK\n");
    busy_delay(20000000);
    
    /* Network driver */
    printk("[    0.000150] Probing network device (e1000)...\n");
    e1000_init();
    printk("[    0.000155] Network driver: OK\n");
    busy_delay(20000000);
    
    /* IP stack */
    printk("[    0.000160] Initializing IP networking stack...\n");
    ip_init();
    printk("[    0.000165] IP networking: OK\n");
    busy_delay(20000000);
    
    /* Filesystem and storage */
    printk("[    0.000170] Initializing storage drivers...\n");
    fs_built_in_init();
    printk("[    0.000180] Storage drivers: OK\n");
    busy_delay(20000000);
    
    /* RAM filesystem */
    printk("[    0.000190] Mounting root filesystem (ramfs)...\n");
    ramfs_init();
    printk("[    0.000195] RAM filesystem: initialized\n");
    fs_mount_root();
    printk("[    0.000200] Root filesystem: mounted\n");
    busy_delay(20000000);

    /* Probe for init binary */
    printk("[    0.000300] Probing for /init.bin\n");
    wait_for_init_bin();

    /* If fs_has_init ever returns true, we would "launch" init here. */
    printk("[    0.600000] Launching init (stub)...\n");
    printk("[    0.670000] Launching init (blob)...\n");
    
    /* Poll input devices while waiting */
    printk("[    0.700000] Polling input devices...\n");
    input_poll();
    
    /* Poll network for incoming packets */
    printk("[    0.710000] Polling network for packets...\n");
    for (int i = 0; i < 100; i++) {
        ip_poll();
        busy_delay(1000000);
    }
    
    busy_delay(20000000);
    printk("[    0.800000] FATAL: Reached end of kernel_main without userspace init\n");
    kernel_panic("init.bin not loaded");
}

/* Filesystem implementation using ramfs for loading init.bin */
u8 fs_has_init(void) {
    return ramfs_has_init();
}

void fs_mount_root(void) {
    ramfs_mount();
}

