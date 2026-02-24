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
    
    printk(T,"Initialized ramfs\n");
}

/**
 * Prepare ramfs for use (called before loading modules)
 * Does NOT clear existing files - just marks as ready
 */
void ramfs_prepare(void) {
    /* Ensure the file table is ready without clearing */
    g_ramfs_mounted = 0;
    printk(T,"Prepared for module loading\n");
}

/**
 * Mount ramfs as the root filesystem
 */
void ramfs_mount(void) {
    if (g_ramfs_mounted) {
        printk(T,"Already mounted\n");
        return;
    }
    
    g_ramfs_mounted = true;
    printk(T,"Root filesystem mounted (%u files)\n", g_ramfs_file_count);
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
        printk(T,"Invalid parameters for add_file\n");
        return 0;
    }
    
    if (g_ramfs_file_count >= RAMFS_MAX_FILES) {
        printk(T,"File table full, cannot add '%s'\n", filename);
        return 0;
    }
    
    /* Check if file already exists */
    if (ramfs_file_exists(filename)) {
        printk(T,"File '%s' already exists\n", filename);
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
    
    printk(T,"Added file '%s' (%u bytes)\n", filename, size);
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
    printk(T,"Files in ramfs (%u total):\n", g_ramfs_file_count);
    for (u32 i = 0; i < g_ramfs_file_count; ++i) {
        if (g_ramfs_files[i].valid) {
            printk(T,"  - %s (%u bytes)\n", 
                   g_ramfs_files[i].filename, 
                   g_ramfs_files[i].size);
        }
    }
}

/**
 * Check if init exists in ramfs
 */
u8 ramfs_has_init(void) {
    return ramfs_file_exists("/init");
}

/**
 * Get the init.bin data
 */
u8 *ramfs_get_init(u32 *out_size) {
    return ramfs_get_file("/init", out_size);
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
/* ------------------------------------------------------------------ */
/*  Directory and device node support (highly docced)                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char path[RAMFS_MAX_FILENAME];
    u32  type;    /* ARK_DEV_BLK / ARK_DEV_CHR / 0 = plain dir */
    u32  major;
    u32  minor;
    u8   valid;
} ramfs_dir_entry_t;

#define RAMFS_MAX_DIRS 32
static ramfs_dir_entry_t g_dirs[RAMFS_MAX_DIRS];
static u32 g_dir_count = 0;

u8 ramfs_mkdir(const char *path) {
    if (!path || g_dir_count >= RAMFS_MAX_DIRS) return 0;
    /* don't duplicate */
    for (u32 i = 0; i < g_dir_count; i++)
        if (g_dirs[i].valid && streq(g_dirs[i].path, path)) return 1;
    ramfs_dir_entry_t *d = &g_dirs[g_dir_count++];
    u32 i = 0;
    while (path[i] && i < RAMFS_MAX_FILENAME - 1) { d->path[i] = path[i]; i++; }
    d->path[i] = '\0';
    d->type  = 0;
    d->major = 0;
    d->minor = 0;
    d->valid = 1;
    return 1;
}

u8 ramfs_mknod(const char *path, u32 type, u32 major, u32 minor) {
    if (!path || g_dir_count >= RAMFS_MAX_DIRS) return 0;
    ramfs_dir_entry_t *d = &g_dirs[g_dir_count++];
    u32 i = 0;
    while (path[i] && i < RAMFS_MAX_FILENAME - 1) { d->path[i] = path[i]; i++; }
    d->path[i] = '\0';
    d->type  = type;
    d->major = major;
    d->minor = minor;
    d->valid = 1;
    return 1;
}

u8 ramfs_dir_exists(const char *path) {
    for (u32 i = 0; i < g_dir_count; i++)
        if (g_dirs[i].valid && streq(g_dirs[i].path, path)) return 1;
    return 0;
}

/* get the name component after the last '/' */
static const char *basename(const char *path) {
    const char *last = path;
    while (*path) { if (*path == '/') last = path + 1; path++; }
    return last;
}

/* check if 'path' is a direct child of 'parent' */
static u8 is_direct_child(const char *parent, const char *path) {
    u32 plen = 0;
    while (parent[plen]) plen++;

    /* path must start with parent */
    for (u32 i = 0; i < plen; i++)
        if (path[i] != parent[i]) return 0;

    /* for root "/", child must be "/something" with no further '/' */
    const char *rest = path + plen;
    if (parent[0] == '/' && parent[1] == '\0') {
        /* root: rest should be "name" no slash */
        if (rest[0] == '/') rest++; /* skip leading slash */
    } else {
        if (rest[0] != '/') return 0;
        rest++;
    }
    /* rest must be non-empty and contain no '/' */
    if (!rest[0]) return 0;
    while (*rest) { if (*rest == '/') return 0; rest++; }
    return 1;
}

/*
 * Count all direct children of 'parent':
 * - ramfs files whose path starts with parent/
 * - dirs/nodes whose path starts with parent/
 */
u32 ramfs_list_count(const char *parent) {
    u32 count = 0;

    /* files */
    for (u32 i = 0; i < g_ramfs_file_count; i++) {
        if (g_ramfs_files[i].valid &&
            is_direct_child(parent, g_ramfs_files[i].filename))
            count++;
    }
    /* dirs and nodes */
    for (u32 i = 0; i < g_dir_count; i++) {
        if (g_dirs[i].valid && is_direct_child(parent, g_dirs[i].path))
            count++;
    }
    return count;
}

/*
 * Get the nth direct child name of 'parent'
 */
u8 ramfs_list_at(const char *parent, u32 index, char *name_out, u32 name_max) {
    u32 cur = 0;

    /* files first */
    for (u32 i = 0; i < g_ramfs_file_count; i++) {
        if (g_ramfs_files[i].valid &&
            is_direct_child(parent, g_ramfs_files[i].filename)) {
            if (cur == index) {
                const char *bn = basename(g_ramfs_files[i].filename);
                u32 j = 0;
                while (bn[j] && j + 1 < name_max) { name_out[j] = bn[j]; j++; }
                name_out[j] = '\0';
                return 1;
            }
            cur++;
        }
    }
    /* then dirs/nodes */
    for (u32 i = 0; i < g_dir_count; i++) {
        if (g_dirs[i].valid && is_direct_child(parent, g_dirs[i].path)) {
            if (cur == index) {
                const char *bn = basename(g_dirs[i].path);
                u32 j = 0;
                while (bn[j] && j + 1 < name_max) { name_out[j] = bn[j]; j++; }
                name_out[j] = '\0';
                return 1;
            }
            cur++;
        }
    }
    return 0;
}