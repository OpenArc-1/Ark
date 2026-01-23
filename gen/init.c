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
#include "pci.h" //new mod created by yahya mokhlis

extern void clear_screen(void);
/* Forward declarations for future subsystems. */
bool fs_has_init(void);
void fs_mount_root(void);

static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

static void wait_for_init_bin(void) {
    /* Linux-like messages while we "wait" for /init.bin to appear.
     * Since there is no real filesystem yet, this just retries a
     * few times and then panics with a clear message.
     */
    for (int attempt = 1; attempt <= 5; ++attempt) {
        printk("[    0.000000] ark-init: waiting for /init.bin (attempt %d/5)\n",
               attempt);
        busy_delay(50000000);
        if (fs_has_init()) {
            printk("[    0.100000] ark-init: found /init.bin, starting userspace\n");
            return;
        }
    }

    printk("[    0.500000] ark-init: /init.bin not found after retries\n");
    kernel_panic("init.bin not found");
}

void kernel_main(void) {
    clear_screen();
    scanAll();
    busy_delay(20000000);
    printk("[    0.000000] Ark kernel booting on x86\n");
    printk("[    0.000001] Boot params: stub (no cmdline yet)\n");

    printk("[    0.000100] Initialising core subsystems...\n");
    /* TODO: initialise memory manager, scheduler, device model, etc. */
    busy_delay(20000000);

    printk("[    0.000200] Mounting root filesystem (stub)...\n");
    fs_mount_root();
    busy_delay(20000000);

    printk("[    0.000300] Probing for /init.bin\n");
    wait_for_init_bin();

    /* If fs_has_init ever returns true, we would "launch" init here. */
    printk("[    0.600000] Launching init (stub)...\n");
    printk("[    0.670000] Launching init (blob)...\n");
    busy_delay(20000000);
    kernel_panic("Reached end of kernel_main without real init");
}

/* Temporary stub filesystem implementation so the kernel builds and
 * demonstrates the panic / init wait path. In the future these
 * should move into fs/fat32.c or another filesystem module.
 */
bool fs_has_init(void) {
    /* TODO: real init.bin detection. For now always "not found". */
    return false;
}

void fs_mount_root(void) {
    printk("[    0.000150] fs: root mount stub\n");
}


