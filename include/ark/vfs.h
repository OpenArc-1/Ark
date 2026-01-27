/**
 * Virtual Filesystem Layer for Ark
 *
 * Provides a common interface for different filesystem implementations
 */

#ifndef VFS_H
#define VFS_H

#include "ark/types.h"

/**
 * File types
 */
enum vfs_file_type {
    VFS_TYPE_REGULAR = 0,
    VFS_TYPE_DIRECTORY = 1,
    VFS_TYPE_SYMLINK = 2,
    VFS_TYPE_DEVICE = 3
};

/**
 * File descriptor structure
 */
typedef struct {
    int fd;
    u32 position;
    u32 size;
    enum vfs_file_type type;
    char path[256];
    u8 valid;
} vfs_file_t;

/**
 * Initialize virtual filesystem
 */
void vfs_init(void);

/**
 * Mount a filesystem
 * @param fs_type Filesystem type (e.g., "ramfs", "fat32")
 * @param device Device path (e.g., "/dev/sda1")
 * @param mount_point Mount point (e.g., "/")
 * @return 0 on success
 */
int vfs_mount(const char *fs_type, const char *device, const char *mount_point);

/**
 * Open a file
 * @param path File path
 * @return File descriptor on success, -1 on error
 */
int vfs_open(const char *path);

/**
 * Read from file
 * @param fd File descriptor
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
int vfs_read(int fd, void *buffer, u32 size);

/**
 * Seek in file
 * @param fd File descriptor
 * @param offset Offset to seek to
 * @return 0 on success
 */
int vfs_seek(int fd, u32 offset);

/**
 * Close file
 * @param fd File descriptor
 * @return 0 on success
 */
int vfs_close(int fd);

/**
 * Get file size
 * @param fd File descriptor
 * @return File size in bytes
 */
u32 vfs_file_size(int fd);

/**
 * Check if file exists
 * @param path File path
 * @return 1 if exists, 0 if not
 */
u8 vfs_file_exists(const char *path);

<<<<<<< HEAD
=======
/**
 * List files at path (ramfs root "/" only).
 * @param path Must be "/"
 * @return Number of files
 */
u32 vfs_list_count(const char *path);

/**
 * Get filename at index (for path "/").
 * @param path Must be "/"
 * @param index 0..vfs_list_count()-1
 * @param name_out Buffer for filename
 * @param name_max Buffer size including NUL
 * @return 1 on success, 0 otherwise
 */
u8 vfs_list_at(const char *path, u32 index, char *name_out, u32 name_max);

>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
#endif /* VFS_H */
