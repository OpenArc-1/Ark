/**
 * FAT32 Filesystem Driver Header
 */

#ifndef FAT32_H
#define FAT32_H

#include "ark/types.h"

/**
 * Initialize FAT32 driver
 */
void fat32_init(void);

/**
 * Mount FAT32 filesystem
 * @param device Device path
 * @return 0 on success, -1 on error
 */
int fat32_mount(const char *device);

/**
 * Open a file on FAT32 filesystem
 * @param path File path
 * @return File descriptor on success, -1 on error
 */
int fat32_open(const char *path);

/**
 * Read from FAT32 file
 * @param fd File descriptor
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
int fat32_read(int fd, void *buffer, u32 size);

/**
 * Close FAT32 file
 * @param fd File descriptor
 * @return 0 on success
 */
int fat32_close(int fd);

/**
 * Get FAT32 file size
 * @param fd File descriptor
 * @return File size in bytes
 */
u32 fat32_file_size(int fd);

#endif /* FAT32_H */
