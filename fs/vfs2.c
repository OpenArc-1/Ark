/**
 * fs/vfs2.c — Ark VFS v2 implementation
 *
 * See vfs2.h for the design rationale.
 *
 * This file is intentionally self-contained: it does not call kmalloc so it
 * can be initialised before the heap.  The open-file table and mount table
 * are static arrays; increase VFS_MAX_FILES / VFS_MAX_MOUNTS when the heap
 * is wired up and replace with kmalloc.
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "fs/vfs2.h"

/* ── Errno shim ──────────────────────────────────────────────────────────── */
#define ENOENT   2
#define EBADF    9
#define ENOMEM  12
#define EACCES  13
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENOSPC  28
#define EROFS   30
#define ENOSYS  38

/* ── Filesystem registry ─────────────────────────────────────────────────── */
#define VFS_MAX_FS_DRIVERS  8

static vfs_fs_ops_t *g_fs_drivers[VFS_MAX_FS_DRIVERS];
static int           g_fs_count = 0;

int vfs2_register_fs(vfs_fs_ops_t *ops) {
    if (!ops || !ops->name) return -EINVAL;
    if (g_fs_count >= VFS_MAX_FS_DRIVERS) return -ENOSPC;
    g_fs_drivers[g_fs_count++] = ops;
    printk("vfs: registered fs '%s'\n", ops->name);
    return 0;
}

static vfs_fs_ops_t *find_fs(const char *name) {
    for (int i = 0; i < g_fs_count; i++)
        if (g_fs_drivers[i] && g_fs_drivers[i]->name) {
            const char *a = g_fs_drivers[i]->name, *b = name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == *b) return g_fs_drivers[i];
        }
    return NULL;
}

/* ── Mount table ─────────────────────────────────────────────────────────── */
typedef struct {
    char         path[VFS_MOUNT_PATH];  /* e.g. "/" or "/mnt" */
    vfs_inode_t *root;                  /* root inode of the mounted fs */
    vfs_fs_ops_t *ops;
    bool          active;
} vfs_mount_t;

static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];
static int         g_mount_count = 0;

int vfs2_mount(const char *fs_name, const char *device, const char *mp) {
    if (!fs_name || !mp) return -EINVAL;
    if (g_mount_count >= VFS_MAX_MOUNTS) return -ENOSPC;

    vfs_fs_ops_t *ops = find_fs(fs_name);
    if (!ops) {
        printk("vfs: unknown filesystem '%s'\n", fs_name);
        return -ENOENT;
    }
    if (!ops->mount) return -ENOSYS;

    vfs_inode_t *root = ops->mount(device);
    if (!root) {
        printk("vfs: mount('%s' on '%s') failed\n", fs_name, mp);
        return -EINVAL;
    }

    vfs_mount_t *m = &g_mounts[g_mount_count++];
    /* Copy mount point path */
    int i = 0;
    while (mp[i] && i < VFS_MOUNT_PATH - 1) { m->path[i] = mp[i]; i++; }
    m->path[i] = '\0';
    m->root   = root;
    m->ops    = ops;
    m->active = true;

    printk("vfs: mounted %s on %s\n", fs_name, mp);
    return 0;
}

int vfs2_umount(const char *mp) {
    for (int i = 0; i < g_mount_count; i++) {
        vfs_mount_t *m = &g_mounts[i];
        if (!m->active) continue;
        /* Compare path */
        int j = 0;
        while (mp[j] && m->path[j] && mp[j] == m->path[j]) j++;
        if (!mp[j] && !m->path[j]) {
            if (m->ops->umount) m->ops->umount(m->root);
            m->active = false;
            printk("vfs: unmounted %s\n", mp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Find the deepest mount point that is a prefix of 'path'.
 * Returns the mount and advances *path_tail past the mount prefix. */
static vfs_mount_t *find_mount(const char *path, const char **path_tail) {
    vfs_mount_t *best = NULL;
    int           best_len = -1;

    for (int i = 0; i < g_mount_count; i++) {
        vfs_mount_t *m = &g_mounts[i];
        if (!m->active) continue;
        int j = 0;
        while (m->path[j] && path[j] && m->path[j] == path[j]) j++;
        /* Match if we consumed the whole mount path and path either ends or
         * has a '/' next (avoids /mnt matching /mnt2). */
        if (!m->path[j] && (path[j] == '/' || path[j] == '\0') && j > best_len) {
            best     = m;
            best_len = j;
        }
    }

    if (best && path_tail) {
        *path_tail = path + best_len;
        if (**path_tail == '/') (*path_tail)++;
    }
    return best;
}

/* ── Inode refcounting ───────────────────────────────────────────────────── */
void vfs2_inode_get(vfs_inode_t *ino) {
    if (ino) ino->refcount++;
}

void vfs2_inode_put(vfs_inode_t *ino) {
    if (!ino) return;
    if (ino->refcount > 0) ino->refcount--;
    if (ino->refcount == 0 && ino->fs && ino->fs->inode_release)
        ino->fs->inode_release(ino);
}

/* ── Path resolution ─────────────────────────────────────────────────────── */
/*
 * Walk each '/' delimited component calling ops->lookup().
 * Does not handle ".." (safe for an initramfs-only kernel at this stage;
 * add dotdot tracking once mounts have back-pointers to parent dirs).
 */
static vfs_inode_t *path_walk(vfs_mount_t *mnt, const char *rel_path) {
    if (!mnt || !mnt->root) return NULL;

    vfs_inode_t *cur = mnt->root;
    vfs2_inode_get(cur);

    /* Empty tail → return the mount root itself */
    if (!rel_path || !*rel_path) return cur;

    char comp[VFS_NAME_MAX + 1];
    const char *p = rel_path;

    while (*p) {
        /* Skip consecutive slashes */
        while (*p == '/') p++;
        if (!*p) break;

        /* Extract one component */
        int ci = 0;
        while (*p && *p != '/' && ci < VFS_NAME_MAX)
            comp[ci++] = *p++;
        comp[ci] = '\0';

        if (ci == 0) break;

        /* cur must be a directory */
        if (!VFS_S_ISDIR(cur->mode)) {
            vfs2_inode_put(cur);
            return NULL;
        }

        /* Lookup component — need the ops from the inode's fs */
        vfs_fs_ops_t *ops = cur->fs;
        if (!ops || !ops->lookup) {
            vfs2_inode_put(cur);
            return NULL;
        }

        vfs_inode_t *next = ops->lookup(cur, comp);
        vfs2_inode_put(cur);
        if (!next) return NULL;
        cur = next;
    }

    return cur;
}

vfs_inode_t *vfs2_path_lookup(const char *path) {
    if (!path || path[0] != '/') return NULL;

    const char *tail;
    vfs_mount_t *mnt = find_mount(path, &tail);
    if (!mnt) return NULL;

    return path_walk(mnt, tail);
}

/* ── Open file table ─────────────────────────────────────────────────────── */
static vfs_file_t g_files[VFS_MAX_FILES];
static bool       g_files_init = false;

static void files_init(void) {
    if (g_files_init) return;
    for (int i = 0; i < VFS_MAX_FILES; i++) g_files[i].valid = false;
    g_files_init = true;
}

static int alloc_fd(void) {
    /* Reserve 0/1/2 for stdin/stdout/stderr — they are handled by the
     * syscall layer and never back an inode at this level. */
    for (int i = 3; i < VFS_MAX_FILES; i++)
        if (!g_files[i].valid) return i;
    return -ENOSPC;
}

/* ── vfs2_open ───────────────────────────────────────────────────────────── */
int vfs2_open(const char *path, u32 flags, u16 mode) {
    files_init();

    const char *tail;
    vfs_mount_t *mnt = find_mount(path, &tail);
    if (!mnt) return -ENOENT;

    vfs_inode_t *ino = path_walk(mnt, tail);

    if (!ino) {
        /* File not found — try to create if O_CREAT */
        if (!(flags & VFS_O_CREAT)) return -ENOENT;

        /* Separate parent path from filename */
        const char *slash = tail;
        const char *last  = tail;
        for (const char *q = tail; *q; q++)
            if (*q == '/') slash = q;
        if (slash != tail) {
            /* Has a parent component — look it up */
            char parent[VFS_MOUNT_PATH];
            int pi = 0;
            while (tail + pi < slash && pi < VFS_MOUNT_PATH - 1) {
                parent[pi] = tail[pi]; pi++;
            }
            parent[pi] = '\0';
            last = slash + 1;

            vfs_inode_t *dir = path_walk(mnt, parent);
            if (!dir) return -ENOENT;
            if (!mnt->ops->create) { vfs2_inode_put(dir); return -EROFS; }
            ino = mnt->ops->create(dir, last, mode | VFS_IFREG);
            vfs2_inode_put(dir);
        } else {
            /* File in mount root */
            if (!mnt->ops->create) return -EROFS;
            ino = mnt->ops->create(mnt->root, tail, mode | VFS_IFREG);
        }
        if (!ino) return -ENOSPC;
    } else if (flags & VFS_O_TRUNC) {
        if (ino->fs && ino->fs->truncate)
            ino->fs->truncate(ino, 0);
    }

    int fd = alloc_fd();
    if (fd < 0) { vfs2_inode_put(ino); return -ENOSPC; }

    g_files[fd].inode = ino;
    g_files[fd].pos   = (flags & VFS_O_APPEND) ? ino->size : 0;
    g_files[fd].flags = flags;
    g_files[fd].valid = true;

    return fd;
}

/* ── vfs2_read ───────────────────────────────────────────────────────────── */
int vfs2_read(int fd, void *buf, u32 len) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].valid) return -EBADF;
    vfs_file_t  *f   = &g_files[fd];
    vfs_inode_t *ino = f->inode;

    if ((f->flags & VFS_O_ACCMODE) == VFS_O_WRONLY) return -EACCES;
    if (!ino->fs || !ino->fs->read) return -ENOSYS;

    /* Clamp to file size */
    if (f->pos >= ino->size) return 0;
    if (f->pos + len > ino->size) len = ino->size - f->pos;

    int n = ino->fs->read(ino, buf, f->pos, len);
    if (n > 0) f->pos += (u32)n;
    return n;
}

/* ── vfs2_write ──────────────────────────────────────────────────────────── */
int vfs2_write(int fd, const void *buf, u32 len) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].valid) return -EBADF;
    vfs_file_t  *f   = &g_files[fd];
    vfs_inode_t *ino = f->inode;

    if ((f->flags & VFS_O_ACCMODE) == VFS_O_RDONLY) return -EACCES;
    if (!ino->fs || !ino->fs->write) return -EROFS;

    /* Append mode: always write at end */
    if (f->flags & VFS_O_APPEND) f->pos = ino->size;

    int n = ino->fs->write(ino, buf, f->pos, len);
    if (n > 0) {
        f->pos += (u32)n;
        if (f->pos > ino->size) ino->size = f->pos;
    }
    return n;
}

/* ── vfs2_seek ───────────────────────────────────────────────────────────── */
int vfs2_seek(int fd, i32 offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].valid) return -EBADF;
    vfs_file_t *f = &g_files[fd];
    i32 new_pos;

    switch (whence) {
    case VFS_SEEK_SET: new_pos = offset; break;
    case VFS_SEEK_CUR: new_pos = (i32)f->pos + offset; break;
    case VFS_SEEK_END: new_pos = (i32)f->inode->size + offset; break;
    default: return -EINVAL;
    }
    if (new_pos < 0) return -EINVAL;
    f->pos = (u32)new_pos;
    return 0;
}

u32 vfs2_tell(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].valid) return (u32)-1;
    return g_files[fd].pos;
}

/* ── vfs2_close ──────────────────────────────────────────────────────────── */
int vfs2_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].valid) return -EBADF;
    vfs2_inode_put(g_files[fd].inode);
    g_files[fd].valid = false;
    g_files[fd].inode = NULL;
    return 0;
}

/* ── vfs2_stat ───────────────────────────────────────────────────────────── */
int vfs2_stat(const char *path, vfs_inode_t *out) {
    vfs_inode_t *ino = vfs2_path_lookup(path);
    if (!ino) return -ENOENT;
    *out = *ino;   /* copy metadata */
    vfs2_inode_put(ino);
    return 0;
}

/* ── vfs2_mkdir ──────────────────────────────────────────────────────────── */
int vfs2_mkdir(const char *path, u16 mode) {
    const char *tail;
    vfs_mount_t *mnt = find_mount(path, &tail);
    if (!mnt) return -ENOENT;
    if (!mnt->ops->mkdir) return -EROFS;

    /* Separate parent / name */
    const char *slash = tail, *name = tail;
    for (const char *q = tail; *q; q++)
        if (*q == '/') slash = q;
    if (slash != tail) name = slash + 1;

    vfs_inode_t *dir;
    if (slash != tail) {
        char parent[VFS_MOUNT_PATH];
        int pi = 0;
        while (tail + pi < slash && pi < VFS_MOUNT_PATH - 1) {
            parent[pi] = tail[pi]; pi++;
        }
        parent[pi] = '\0';
        dir = path_walk(mnt, parent);
    } else {
        dir = mnt->root;
        vfs2_inode_get(dir);
    }
    if (!dir) return -ENOENT;

    vfs_inode_t *new_dir = mnt->ops->mkdir(dir, name, mode | VFS_IFDIR);
    vfs2_inode_put(dir);
    if (!new_dir) return -EEXIST;
    vfs2_inode_put(new_dir);
    return 0;
}

/* ── vfs2_unlink ─────────────────────────────────────────────────────────── */
int vfs2_unlink(const char *path) {
    const char *tail;
    vfs_mount_t *mnt = find_mount(path, &tail);
    if (!mnt) return -ENOENT;
    if (!mnt->ops->unlink) return -EROFS;

    const char *slash = tail, *name = tail;
    for (const char *q = tail; *q; q++)
        if (*q == '/') slash = q;
    if (slash != tail) name = slash + 1;

    vfs_inode_t *dir;
    if (slash != tail) {
        char parent[VFS_MOUNT_PATH];
        int pi = 0;
        while (tail + pi < slash && pi < VFS_MOUNT_PATH - 1) {
            parent[pi] = tail[pi]; pi++;
        }
        parent[pi] = '\0';
        dir = path_walk(mnt, parent);
    } else {
        dir = mnt->root;
        vfs2_inode_get(dir);
    }
    if (!dir) return -ENOENT;
    int r = mnt->ops->unlink(dir, name);
    vfs2_inode_put(dir);
    return r;
}

/* ── Directory iteration ─────────────────────────────────────────────────── */
/*
 * vfs2_opendir() is just vfs2_open() with the directory flag checked.
 * vfs2_readdir() delegates to the filesystem's readdir op, passing the
 * file position as the cookie.
 */
int vfs2_opendir(const char *path) {
    int fd = vfs2_open(path, VFS_O_RDONLY, 0);
    if (fd < 0) return fd;
    if (!VFS_S_ISDIR(g_files[fd].inode->mode)) {
        vfs2_close(fd);
        return -ENOTDIR;
    }
    return fd;
}

int vfs2_readdir(int fd, vfs_dirent_t *out) {
    if (fd < 0 || fd >= VFS_MAX_FILES || !g_files[fd].valid) return -EBADF;
    vfs_file_t  *f   = &g_files[fd];
    vfs_inode_t *ino = f->inode;
    if (!VFS_S_ISDIR(ino->mode)) return -ENOTDIR;
    if (!ino->fs || !ino->fs->readdir) return -ENOSYS;
    return ino->fs->readdir(ino, &f->pos, out);
}

int vfs2_closedir(int fd) {
    return vfs2_close(fd);
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void vfs2_init(void) {
    files_init();
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) g_mounts[i].active = false;
    g_mount_count = 0;
    g_fs_count    = 0;
    printk("vfs2: initialised (max_files=%d max_mounts=%d)\n",
           VFS_MAX_FILES, VFS_MAX_MOUNTS);
}

/* ── Diagnostics ─────────────────────────────────────────────────────────── */
void vfs2_dump_mounts(void) {
    printk("vfs2 mounts:\n");
    for (int i = 0; i < g_mount_count; i++) {
        vfs_mount_t *m = &g_mounts[i];
        if (m->active)
            printk("  %-24s  %s  root_ino=%u\n",
                   m->path, m->ops->name, m->root ? m->root->ino : 0);
    }
}

void vfs2_dump_open_files(void) {
    printk("vfs2 open files:\n");
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        vfs_file_t *f = &g_files[i];
        if (!f->valid) continue;
        printk("  fd=%-3d ino=%-6u pos=%-8u flags=0x%x mode=0x%x\n",
               i, f->inode->ino, f->pos, f->flags, f->inode->mode);
    }
}
