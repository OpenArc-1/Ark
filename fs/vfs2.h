/**
 * fs/vfs2.h — Ark VFS v2: inode-based virtual filesystem layer
 *
 * Problems with the current VFS (vfs.c)
 * ───────────────────────────────────────
 * 1. NO INODE ABSTRACTION.  Every open() scans the path through ramfs, then
 *    fat32, then afs, sequentially.  There is no concept of a mounted
 *    filesystem tree or a dentry cache.  Two open() calls on the same path
 *    create two independent state objects with no shared metadata.
 *
 * 2. HARDCODED FILESYSTEM ORDER.  Adding a new filesystem requires editing
 *    vfs_open(), vfs_read(), etc.  There is no ops-table dispatch.
 *
 * 3. MAX_OPEN_FILES = 16.  A shell + editor + network daemon will exhaust
 *    this immediately.  The limit is purely artificial; a small heap allocator
 *    removes it entirely.
 *
 * 4. vfs_write() is a stub (always returns -1 EPERM).  Even stdout goes through
 *    a special-case in the syscall layer rather than through VFS.
 *
 * 5. No directory iteration from vfs_open().  vfs_list_count() / vfs_list_at()
 *    bypass the abstraction and call ramfs directly.
 *
 * New design (inode + ops table)
 * ─────────────────────────────
 *  vfs_inode_t       — per-file metadata (type, size, permissions, fs-private ptr)
 *  vfs_file_t        — open file handle (inode + position + flags)
 *  vfs_fs_ops_t      — per-filesystem function table (lookup, read, write, …)
 *  vfs_mount_point_t — maps a path prefix to a filesystem + root inode
 *
 * The lookup() op traverses path components and returns an inode.
 * open/read/write/close operate on file handles that reference inodes.
 * This is structurally similar to Linux's VFS minus the dcache.
 */

#pragma once
#include "ark/types.h"

/* ── File type flags ─────────────────────────────────────────────────────── */
#define VFS_IFMT   0xF000u
#define VFS_IFREG  0x8000u   /* regular file  */
#define VFS_IFDIR  0x4000u   /* directory     */
#define VFS_IFBLK  0x6000u   /* block device  */
#define VFS_IFCHR  0x2000u   /* char device   */
#define VFS_IFSYM  0xA000u   /* symlink       */

#define VFS_S_ISREG(m) (((m) & VFS_IFMT) == VFS_IFREG)
#define VFS_S_ISDIR(m) (((m) & VFS_IFMT) == VFS_IFDIR)

/* ── Open flags ──────────────────────────────────────────────────────────── */
#define VFS_O_RDONLY  0x0000u
#define VFS_O_WRONLY  0x0001u
#define VFS_O_RDWR    0x0002u
#define VFS_O_CREAT   0x0040u
#define VFS_O_TRUNC   0x0200u
#define VFS_O_APPEND  0x0400u
#define VFS_O_ACCMODE 0x0003u

/* ── Seek whence ─────────────────────────────────────────────────────────── */
#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

/* ── Inode ───────────────────────────────────────────────────────────────── */
#define VFS_NAME_MAX  255

typedef struct vfs_inode {
    u32   ino;          /* inode number (unique within a mounted fs)    */
    u16   mode;         /* VFS_IFxxx | permission bits                  */
    u32   size;         /* byte size (0 for directories)                */
    u32   nlinks;       /* hard link count                              */

    /* Filesystem that owns this inode.  NULL for synthetic inodes. */
    struct vfs_fs_ops *fs;
    void  *fs_private;  /* opaque pointer — e.g. ramfs_file_t *         */

    /* Refcount for open files — inode released when it hits 0 */
    u32   refcount;
} vfs_inode_t;

/* ── Directory entry (returned by readdir) ───────────────────────────────── */
typedef struct {
    u32  ino;
    u16  type;          /* VFS_IFxxx */
    char name[VFS_NAME_MAX + 1];
} vfs_dirent_t;

/* ── Per-filesystem operations table ─────────────────────────────────────── */
/*
 * Each registered filesystem provides this table.
 * NULL slots mean "not supported" — callers check before calling.
 *
 * Naming follows Linux VFS conventions where applicable.
 */
typedef struct vfs_fs_ops {
    const char *name;   /* "ramfs", "fat32", "afs", "devfs", … */

    /* Resolve a path component starting from inode 'dir'.
     * Returns a newly-referenced inode or NULL on failure. */
    vfs_inode_t *(*lookup)(vfs_inode_t *dir, const char *name);

    /* Create a new regular file below 'dir'.  Return its inode. */
    vfs_inode_t *(*create)(vfs_inode_t *dir, const char *name, u16 mode);

    /* Create a directory. */
    vfs_inode_t *(*mkdir)(vfs_inode_t *dir, const char *name, u16 mode);

    /* Unlink (delete) a name from a directory. */
    int          (*unlink)(vfs_inode_t *dir, const char *name);

    /* Read up to 'len' bytes from inode at 'offset'. */
    int          (*read)(vfs_inode_t *ino, void *buf, u32 offset, u32 len);

    /* Write up to 'len' bytes to inode at 'offset'. */
    int          (*write)(vfs_inode_t *ino, const void *buf, u32 offset, u32 len);

    /* Truncate / set file size. */
    int          (*truncate)(vfs_inode_t *ino, u32 new_size);

    /* Iterate directory entries.  *cookie is 0 on first call; updated on each.
     * Returns 1 if an entry was written, 0 at end, -1 on error. */
    int          (*readdir)(vfs_inode_t *dir, u32 *cookie, vfs_dirent_t *out);

    /* Called when the last reference to an inode is released. */
    void         (*inode_release)(vfs_inode_t *ino);

    /* Return the root inode of a freshly mounted instance.
     * 'device' is e.g. "/dev/sda1" or NULL for pseudo-filesystems. */
    vfs_inode_t *(*mount)(const char *device);

    /* Called when the filesystem is unmounted. */
    void         (*umount)(vfs_inode_t *root);
} vfs_fs_ops_t;

/* ── Open file handle ─────────────────────────────────────────────────────── */
typedef struct vfs_file {
    vfs_inode_t *inode;
    u32          pos;
    u32          flags;   /* VFS_O_* */
    bool         valid;
} vfs_file_t;

/* ── Mount point ──────────────────────────────────────────────────────────── */
#define VFS_MAX_MOUNTS   8
#define VFS_MAX_FILES    64   /* open file table — increase with kmalloc */
#define VFS_MOUNT_PATH   256

/* ── Public API ───────────────────────────────────────────────────────────── */

/* One-time init.  Call before any other vfs2_ function. */
void vfs2_init(void);

/* Register a filesystem driver (can be called before vfs2_init). */
int  vfs2_register_fs(vfs_fs_ops_t *ops);

/* Mount 'fs_name' on 'mount_point', backing device 'device' (or NULL). */
int  vfs2_mount(const char *fs_name, const char *device, const char *mount_point);

/* Unmount by mount point. */
int  vfs2_umount(const char *mount_point);

/* Resolve an absolute path to an inode (does NOT open it).
 * Caller must call vfs2_inode_put() when done. */
vfs_inode_t *vfs2_path_lookup(const char *path);

/* Open file.  Returns fd >= 0 on success, -errno on error. */
int  vfs2_open(const char *path, u32 flags, u16 mode);

int  vfs2_read(int fd, void *buf, u32 len);
int  vfs2_write(int fd, const void *buf, u32 len);
int  vfs2_seek(int fd, i32 offset, int whence);
int  vfs2_close(int fd);
u32  vfs2_tell(int fd);

int  vfs2_stat(const char *path, vfs_inode_t *out);   /* copy inode metadata */
int  vfs2_mkdir(const char *path, u16 mode);
int  vfs2_unlink(const char *path);

/* Directory iteration */
int  vfs2_opendir(const char *path);                  /* returns fd */
int  vfs2_readdir(int fd, vfs_dirent_t *out);
int  vfs2_closedir(int fd);

/* Inode refcount management */
void vfs2_inode_get(vfs_inode_t *ino);
void vfs2_inode_put(vfs_inode_t *ino);

/* Debugging */
void vfs2_dump_mounts(void);
void vfs2_dump_open_files(void);
