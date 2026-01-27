/**
 * Virtual Filesystem Layer Implementation
 */

#include "ark/vfs.h"
#include "ark/ramfs.h"
#include "ark/fat32.h"
#include "ark/printk.h"

/* Helper string functions */
static int vfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static void vfs_strncpy(char *dest, const char *src, u32 n) {
    for (u32 i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    if (n > 0) dest[n - 1] = '\0';
}

static void vfs_memcpy(void *dest, const void *src, u32 n) {
    u8 *d = (u8 *)dest;
    u8 *s = (u8 *)src;
    while (n--) {
        *d++ = *s++;
    }
}

#define MAX_OPEN_FILES 16
#define MAX_MOUNTS 4

typedef struct {
    char fs_type[32];
    char device[64];
    char mount_point[256];
    u8 mounted;
} vfs_mount_t;

static vfs_file_t open_files[MAX_OPEN_FILES];
static vfs_mount_t mounts[MAX_MOUNTS];
static u32 mount_count = 0;

void vfs_init(void) {
    printk("[vfs] Virtual filesystem layer initializing...\n");
    
    /* Initialize file table */
    for (u32 i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i].valid = 0;
    }
    
    mount_count = 0;
    
    printk("[vfs] VFS ready\n");
}

int vfs_mount(const char *fs_type, const char *device, const char *mount_point) {
    if (mount_count >= MAX_MOUNTS) {
        printk("[vfs] Error: Too many mounts\n");
        return -1;
    }
    
    printk("[vfs] Mounting %s on %s (type: %s)\n", device, mount_point, fs_type);
    
    /* Store mount info */
    vfs_mount_t *mount = &mounts[mount_count];
    vfs_strncpy(mount->fs_type, fs_type, sizeof(mount->fs_type) - 1);
    vfs_strncpy(mount->device, device, sizeof(mount->device) - 1);
    vfs_strncpy(mount->mount_point, mount_point, sizeof(mount->mount_point) - 1);
    mount->mounted = 1;
    
    mount_count++;
    
    /* Initialize specific filesystem */
    if (vfs_strcmp(fs_type, "ramfs") == 0) {
        ramfs_mount();
        return 0;
    } else if (vfs_strcmp(fs_type, "fat32") == 0) {
        return fat32_mount(device);
    }
    
    printk("[vfs] Error: Unknown filesystem type %s\n", fs_type);
    return -1;
}

int vfs_open(const char *path) {
    /* Find first available file descriptor */
    for (u32 i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].valid) {
            vfs_file_t *file = &open_files[i];
            file->fd = i;
            file->position = 0;
            file->valid = 1;
            
            vfs_strncpy(file->path, path, sizeof(file->path) - 1);
            
            /* Try to open in ramfs first */
            u32 size = 0;
            if (ramfs_file_exists(path)) {
                u8 *data = ramfs_get_file(path, &size);
                if (data) {
                    file->size = size;
                    file->type = VFS_TYPE_REGULAR;
                    return file->fd;
                }
            }
            
            /* Try FAT32 */
            int fat32_fd = fat32_open(path);
            if (fat32_fd >= 0) {
                file->size = fat32_file_size(fat32_fd);
                file->type = VFS_TYPE_REGULAR;
                return file->fd;
            }
            
            file->valid = 0;
            return -1;
        }
    }
    
    return -1;
}

int vfs_read(int fd, void *buffer, u32 size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].valid) {
        return -1;
    }
    
    vfs_file_t *file = &open_files[fd];
    
    /* Clamp read size to file size */
    if (file->position + size > file->size) {
        size = file->size - file->position;
    }
    
    if (size == 0) {
        return 0;
    }
    
    /* Try to read from ramfs */
    u32 file_size = 0;
    if (ramfs_file_exists(file->path)) {
        u8 *data = ramfs_get_file(file->path, &file_size);
        if (data) {
            vfs_memcpy(buffer, data + file->position, size);
            file->position += size;
            return size;
        }
    }
    
    /* Try FAT32 */
    int bytes = fat32_read(fd, buffer, size);
    if (bytes > 0) {
        file->position += bytes;
    }
    
    return bytes;
}

int vfs_seek(int fd, u32 offset) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].valid) {
        return -1;
    }
    
    vfs_file_t *file = &open_files[fd];
    
    if (offset > file->size) {
        return -1;
    }
    
    file->position = offset;
    return 0;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].valid) {
        return -1;
    }
    
    open_files[fd].valid = 0;
    fat32_close(fd);
    
    return 0;
}

u32 vfs_file_size(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].valid) {
        return 0;
    }
    
    return open_files[fd].size;
}

u8 vfs_file_exists(const char *path) {
    return ramfs_file_exists(path) || (fat32_open(path) >= 0);
}
<<<<<<< HEAD
=======

static u8 vfs_path_is_root(const char *path) {
    return path && path[0] == '/' && path[1] == '\0';
}

u32 vfs_list_count(const char *path) {
    if (!vfs_path_is_root(path))
        return 0;
    return ramfs_get_file_count();
}

u8 vfs_list_at(const char *path, u32 index, char *name_out, u32 name_max) {
    if (!vfs_path_is_root(path) || !name_out || name_max == 0)
        return 0;
    char tmp[RAMFS_MAX_FILENAME];
    u8 *data = NULL;
    u32 size = 0;
    if (!ramfs_get_file_by_index(index, tmp, &data, &size))
        return 0;
    vfs_strncpy(name_out, tmp, name_max);
    return 1;
}
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
