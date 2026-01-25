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
