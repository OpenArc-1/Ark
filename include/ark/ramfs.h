/**
 * Ark RAM Filesystem (ramfs)
 *
 * A simple in-memory filesystem for loading kernel images and init binaries.
 * Files are stored in RAM at boot time and can be accessed by the kernel.
 */

#pragma once

#include "ark/types.h"

/**
 * Maximum number of files that can be stored in ramfs
 */
#define RAMFS_MAX_FILES 32

/**
 * Maximum filename length (including null terminator)
 */
#define RAMFS_MAX_FILENAME 256

/**
 * File entry in the ramfs
 */
typedef struct {
    char filename[RAMFS_MAX_FILENAME];
    u8 *data;
    u32 size;
    u8 valid;
} ramfs_file_t;

/**
 * Initialize the ramfs
 */
void ramfs_init(void);

/**
 * Prepare ramfs for use (e.g., before loading modules)
 * Does NOT clear existing files
 */
void ramfs_prepare(void);

/**
 * Mount the ramfs as root filesystem
 */
void ramfs_mount(void);

/**
 * Add a file to the ramfs
 * @param filename Path to the file (e.g., "/init.bin")
 * @param data Pointer to file data
 * @param size Size of the file in bytes
 * @return true if file was added successfully, false otherwise
 */
u8 ramfs_add_file(const char *filename, u8 *data, u32 size);

/**
 * Check if a file exists in the ramfs
 * @param filename Path to the file
 * @return true if file exists, false otherwise
 */
u8 ramfs_file_exists(const char *filename);

/**
 * Get a file from the ramfs
 * @param filename Path to the file
 * @param out_size Pointer to store the file size
 * @return Pointer to file data, or NULL if not found
 */
u8 *ramfs_get_file(const char *filename, u32 *out_size);

/**
 * List all files in ramfs (for debugging)
 */
void ramfs_list_files(void);

/**
 * Check if init.bin exists in ramfs
 * @return true if /init.bin exists, false otherwise
 */
u8 ramfs_has_init(void);

/**
 * Get the init.bin data
 * @param out_size Pointer to store the init.bin size
 * @return Pointer to init.bin data, or NULL if not found
 */
u8 *ramfs_get_init(u32 *out_size);

/**
 * Get file count in ramfs
 * @return Number of files in ramfs
 */
u32 ramfs_get_file_count(void);

/**
 * Get file by index (for iteration)
 * @param index File index (0 to file_count-1)
 * @param out_filename Buffer to store filename (must be at least RAMFS_MAX_FILENAME bytes)
 * @param out_data Pointer to store file data pointer
 * @param out_size Pointer to store file size
 * @return 1 if file exists at index, 0 otherwise
 */
u8 ramfs_get_file_by_index(u32 index, char *out_filename, u8 **out_data, u32 *out_size);

/* helper for log subsystem or other kernel code to adjust an existing file */
/**
 * Update the size of an already-added ramfs file.  The caller is
 * responsible for growing the backing buffer if necessary; this
 * function only adjusts the metadata and will return false if the
 * named file does not exist.
 */
u8 ramfs_set_file_size(const char *filename, u32 size);

/**
 * Replace the data pointer (and size) for an existing file.  Useful if
 * the caller has moved the buffer to a larger allocation but still
 * wants the ramfs entry to reference the new pointer.
 */
u8 ramfs_set_file_data(const char *filename, u8 *data, u32 size);

u8  ramfs_mkdir(const char *path);
u8  ramfs_mknod(const char *path, u32 type, u32 major, u32 minor);
u8  ramfs_dir_exists(const char *path);
u32 ramfs_list_count(const char *parent);
u8  ramfs_list_at(const char *parent, u32 index, char *name_out, u32 name_max);