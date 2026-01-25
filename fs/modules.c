/**
 * Multiboot Module Loader Implementation
 *
 * Reads modules from multiboot info and registers them into ramfs.
 */

#include "modules.h"
#include "ramfs.h"
#include "ark/printk.h"

/**
 * Load all modules from Multiboot info into ramfs
 */
u32 modules_load_from_multiboot(multiboot_info_t *mbi) {
    if (!mbi) {
        return 0;
    }

    u32 mod_count = mbi->mods_count;
    
    if (mod_count == 0) {
        printk("[modules] No modules found in multiboot info\n");
        return 0;
    }

    if (!(mbi->flags & 0x08)) {  /* Check if modules flag is set */
        printk("[modules] Modules flag not set in multiboot info\n");
        return 0;
    }

    multiboot_module_t *mods = (multiboot_module_t *)(u32)mbi->mods_addr;

    printk("[modules] Found %u module(s) from bootloader\n", mod_count);

    u32 loaded = 0;
    for (u32 i = 0; i < mod_count; ++i) {
        u32 start = mods[i].mod_start;
        u32 end = mods[i].mod_end;
        u32 size = end - start;
        u8 *data = (u8 *)(u32)start;

        /* Get module name from command line string */
        const char *name = "/init.bin";  /* Default name */
        if (mods[i].string) {
            const char *cmdline = (const char *)(u32)mods[i].string;
            /* Use first word of cmdline as the module name */
            if (cmdline[0] == '/') {
                name = cmdline;
            } else {
                /* Prepend / if not present */
                name = cmdline;
            }
        }

        printk("[modules] Loading module %u: %s (%u bytes @ 0x%x)\n", 
               i + 1, name, size, start);

        if (!ramfs_add_file(name, data, size)) {
            printk("[modules] Failed to load module '%s' into ramfs\n", name);
        } else {
            ++loaded;
        }
    }

    printk("[modules] Successfully loaded %u module(s) into ramfs\n", loaded);
    return loaded;
}
