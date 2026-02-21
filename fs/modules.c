/**
 * Multiboot Module Loader Implementation
 *
 * Reads modules from multiboot info and registers them into ramfs.
 */

#include "ark/modules.h"
#include "ark/ramfs.h"
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
        printk(T,"No modules found in multiboot info\n");
        return 0;
    }

    if (!(mbi->flags & 0x08)) {  /* Check if modules flag is set */
        printk(T,"Modules flag not set in multiboot info\n");
        return 0;
    }

    multiboot_module_t *mods = (multiboot_module_t *)(u32)mbi->mods_addr;

    printk(T,"Found %u module(s) from bootloader\n", mod_count);

    u32 loaded = 0;
    for (u32 i = 0; i < mod_count; ++i) {
        u32 start = mods[i].mod_start;
        u32 end = mods[i].mod_end;
        u32 size = end - start;
        u8 *data = (u8 *)(u32)start;

        /* Get module name from command line string */
        char filename[256];
        const char *cmdline = "";
        
        if (mods[i].string) {
            cmdline = (const char *)(u32)mods[i].string;
        }
        
        /* Create filename with leading slash */
        if (cmdline[0] == '/') {
            /* Already has leading slash */
            int j = 0;
            while (cmdline[j] && j < 255) {
                filename[j] = cmdline[j];
                j++;
            }
            filename[j] = '\0';
        } else if (cmdline[0] != '\0') {
            /* Prepend slash */
            filename[0] = '/';
            int j = 0;
            while (cmdline[j] && j < 254) {
                filename[j + 1] = cmdline[j];
                j++;
            }
            filename[j + 1] = '\0';
        } else {
            /* No name provided, use default */
            int j = 0;
            const char *default_name = "/init";
            while (default_name[j]) {
                filename[j] = default_name[j];
                j++;
            }
            filename[j] = '\0';
        }

        printk(T,"Loading module %u: %s (%u bytes @ 0x%x)\n", 
               i + 1, filename, size, start);

        if (!ramfs_add_file(filename, data, size)) {
            printk(T,"Failed to load module '%s' into ramfs\n", filename);
        } else {
            ++loaded;
            printk(T,"Successfully added '%s' to ramfs\n", filename);
        }
    }

    printk(T,"Successfully loaded %u module(s) into ramfs\n", loaded);
    return loaded;
}
