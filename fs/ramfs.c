/**
 * Ark RAM Filesystem (ramfs) Implementation
 *
 * Provides a simple in-memory filesystem for storing and retrieving files.
 */

#include "ark/ramfs.h"
#include "ark/printk.h"
#include "ark/types.h"

/* Static ramfs file table */
static ramfs_file_t g_ramfs_files[RAMFS_MAX_FILES];
static u32 g_ramfs_file_count = 0;
static u8 g_ramfs_mounted = 0;

/**
 * Initialize the ramfs (clears file table)
 */
void ramfs_init(void) {
    g_ramfs_file_count = 0;
    g_ramfs_mounted = 0;
    
    for (u32 i = 0; i < RAMFS_MAX_FILES; ++i) {
        g_ramfs_files[i].valid = 0;
        g_ramfs_files[i].data = NULL;
        g_ramfs_files[i].size = 0;
        g_ramfs_files[i].filename[0] = '\0';
    }
    
    printk("[ramfs] Initialized ramfs\n");
}

/**
 * Prepare ramfs for use (called before loading modules)
 * Does NOT clear existing files - just marks as ready
 */
void ramfs_prepare(void) {
    /* Ensure the file table is ready without clearing */
    g_ramfs_mounted = 0;
    printk("[ramfs] Prepared for module loading\n");
}

/**
 * Mount ramfs as the root filesystem
 */
void ramfs_mount(void) {
    if (g_ramfs_mounted) {
        printk("[ramfs] Already mounted\n");
        return;
    }
    
    g_ramfs_mounted = true;
    printk("[ramfs] Root filesystem mounted (%u files)\n", g_ramfs_file_count);
}

/**
 * Utility function to compare strings
 */
static u8 streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

/**
 * Add a file to the ramfs
 */
u8 ramfs_add_file(const char *filename, u8 *data, u32 size) {
    if (!filename || !data || size == 0) {
        printk("[ramfs] Invalid parameters for add_file\n");
        return 0;
    }
    
    if (g_ramfs_file_count >= RAMFS_MAX_FILES) {
        printk("[ramfs] File table full, cannot add '%s'\n", filename);
        return 0;
    }
    
    /* Check if file already exists */
    if (ramfs_file_exists(filename)) {
        printk("[ramfs] File '%s' already exists\n", filename);
        return 0;
    }
    
    ramfs_file_t *entry = &g_ramfs_files[g_ramfs_file_count];
    
    /* Copy filename */
    u32 i = 0;
    while (filename[i] && i < RAMFS_MAX_FILENAME - 1) {
        entry->filename[i] = filename[i];
        ++i;
    }
    entry->filename[i] = '\0';
    
    entry->data = data;
    entry->size = size;
    entry->valid = 1;
    
    ++g_ramfs_file_count;
    
    printk("[ramfs] Added file '%s' (%u bytes)\n", filename, size);
    return 1;
}

/**
 * Check if a file exists in the ramfs
 */
u8 ramfs_file_exists(const char *filename) {
    for (u32 i = 0; i < g_ramfs_file_count; ++i) {
        if (g_ramfs_files[i].valid && streq(g_ramfs_files[i].filename, filename)) {
            return true;
        }
    }
    return false;
}

/**
 * Get a file from the ramfs
 */
u8 *ramfs_get_file(const char *filename, u32 *out_size) {
    if (!filename || !out_size) {
        return NULL;
    }
    
    for (u32 i = 0; i < g_ramfs_file_count; ++i) {
        if (g_ramfs_files[i].valid && streq(g_ramfs_files[i].filename, filename)) {
            *out_size = g_ramfs_files[i].size;
            return g_ramfs_files[i].data;
        }
    }
    
    return NULL;
}

/**
 * List all files in ramfs (for debugging)
 */
void ramfs_list_files(void) {
    printk("[ramfs] Files in ramfs (%u total):\n", g_ramfs_file_count);
    for (u32 i = 0; i < g_ramfs_file_count; ++i) {
        if (g_ramfs_files[i].valid) {
            printk("[ramfs]   - %s (%u bytes)\n", 
                   g_ramfs_files[i].filename, 
                   g_ramfs_files[i].size);
        }
    }
}

/**
 * Check if init.bin exists in ramfs
 */
u8 ramfs_has_init(void) {
    return ramfs_file_exists("/init.bin");
}

/**
 * Get the init.bin data
 */
u8 *ramfs_get_init(u32 *out_size) {
    return ramfs_get_file("/init.bin", out_size);
}

/**
 * Get file count in ramfs
 */
u32 ramfs_get_file_count(void) {
    return g_ramfs_file_count;
}

/**
 * Get file by index (for iteration)
 * @param index File index (0 to file_count-1)
 * @param out_filename Buffer to store filename (must be at least RAMFS_MAX_FILENAME bytes)
 * @param out_data Pointer to store file data pointer
 * @param out_size Pointer to store file size
 * @return 1 if file exists at index, 0 otherwise
 */
u8 ramfs_get_file_by_index(u32 index, char *out_filename, u8 **out_data, u32 *out_size) {
    if (index >= g_ramfs_file_count || !out_filename || !out_data || !out_size)
        return 0;
    
    if (!g_ramfs_files[index].valid)
        return 0;
    
    /* Copy filename */
    u32 i = 0;
    while (g_ramfs_files[index].filename[i] && i < RAMFS_MAX_FILENAME - 1) {
        out_filename[i] = g_ramfs_files[index].filename[i];
        i++;
    }
    out_filename[i] = '\0';
    
    *out_data = g_ramfs_files[index].data;
    *out_size = g_ramfs_files[index].size;
    
    return 1;
}
