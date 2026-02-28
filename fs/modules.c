/**
 * Multiboot Module Loader -- Linux-like initramfs support
 *
 * Boot flow (mirrors Linux initramfs):
 *   1. Bootloader loads kernel + initramfs.zip as multiboot modules
 *   2. This code detects PK (ZIP) modules and extracts them into ramfs
 *   3. kernel_main() mounts ramfs as / and executes /init
 *
 * A non-ZIP module is treated as a raw /init binary (legacy mode).
 */

#include "ark/modules.h"
#include "ark/ramfs.h"
#include "ark/zip.h"
#include "ark/printk.h"

/**
 * Load all modules from Multiboot info into ramfs.
 * ZIP modules are extracted (initramfs-style); raw binaries become /init.
 */
u32 modules_load_from_multiboot(multiboot_info_t *mbi) {
    if (!mbi)
        return 0;

    if (!(mbi->flags & 0x08)) {
        printk(T, "initramfs: multiboot modules flag not set\n");
        return 0;
    }

    u32 mod_count = mbi->mods_count;
    if (mod_count == 0) {
        printk(T, "initramfs: no modules passed by bootloader\n");
        return 0;
    }

    multiboot_module_t *mods = (multiboot_module_t *)(u32)mbi->mods_addr;
    printk(T, "initramfs: %u module(s) from bootloader\n", mod_count);

    u32 total_loaded = 0;

    for (u32 i = 0; i < mod_count; ++i) {
        u32  start = mods[i].mod_start;
        u32  end   = mods[i].mod_end;
        u32  size  = end - start;
        u8  *data  = (u8 *)(u32)start;

        if (size == 0) {
            printk(T, "initramfs: module %u is empty, skipping\n", i + 1);
            continue;
        }

        /* -- ZIP initramfs (Linux-style) -- */
        if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
            printk(T, "initramfs: module %u is a ZIP initramfs (%u bytes @ 0x%x)\n",
                   i + 1, size, start);

            u32 n = zip_load_into_ramfs(data, size);
            if (n > 0) {
                total_loaded += n;
                printk(T, "initramfs: extracted %u file(s) into ramfs\n", n);

                /* Warn if /init is missing -- like Linux's "No init found" panic */
                if (!ramfs_file_exists("/init")) {
                    printk(T, "initramfs: WARNING -- /init not found in ZIP!\n");
                    printk(T, "initramfs:   ZIP must contain a file named 'init'\n");
                    printk(T, "initramfs:   build with: scripts/make_initramfs.sh\n");
                }
            } else {
                printk(T, "initramfs: ZIP parse failed or empty archive\n");
            }
            continue;
        }

        /* -- Raw ELF / script (legacy: single-file /init) -- */
        const char *cmdline = (mods[i].string)
                              ? (const char *)(u32)mods[i].string
                              : "";

        /* Derive ramfs path from cmdline, default /init */
        char filename[256];
        if (cmdline[0] == '\0') {
            filename[0]='/'; filename[1]='i'; filename[2]='n';
            filename[3]='i'; filename[4]='t'; filename[5]='\0';
        } else {
            u32 j = 0;
            if (cmdline[0] != '/') filename[j++] = '/';
            for (u32 k = 0; cmdline[k] && j < 254; k++)
                filename[j++] = cmdline[k];
            filename[j] = '\0';
        }

        printk(T, "initramfs: module %u: raw binary -> %s (%u bytes @ 0x%x)\n",
               i + 1, filename, size, start);

        if (ramfs_add_file(filename, data, size)) {
            ++total_loaded;
            printk(T, "initramfs: added '%s' to ramfs\n", filename);
        } else {
            printk(T, "initramfs: failed to add '%s'\n", filename);
        }
    }

    printk(T, "initramfs: %u file(s) total in ramfs\n", total_loaded);
    return total_loaded;
}
