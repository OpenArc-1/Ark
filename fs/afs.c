/**
 * AFS - Ark File System Driver
 *
 * Custom filesystem for the Ark OS kernel.
 * Supports disks up to 512 GB with both GPT and MBR partition tables.
 *
 * Disk layout (GPT):
 *   LBA 0         : Protective MBR
 *   LBA 1         : GPT Header
 *   LBA 2–33      : GPT Partition Entries
 *   LBA 34+       : AFS Superblock, then data
 *
 * Disk layout (MBR):
 *   LBA 0         : MBR (partition type 0xAF = AFS)
 *   LBA (part)+0  : AFS Superblock
 *   LBA (part)+1  : Block Allocation Bitmap
 *   LBA (part)+N  : Inode Table
 *   LBA (part)+M  : Data blocks
 */

#include "ark/afs.h"
#include "ark/ata.h"
#include "ark/ramfs.h"
#include "ark/printk.h"

/* ── Internal constants ──────────────────────────────────────────── */
#define AFS_MAX_FDS         16
#define AFS_MAX_NAME        255
#define AFS_SUPERBLOCK_LBA  0   /* relative to partition start */
#define AFS_BITMAP_LBA      1
#define AFS_INODE_TABLE_LBA 9   /* after bitmap (up to 8 bitmap sectors) */

/* ── Static sector buffers (no heap) ────────────────────────────── */
static u8 g_sector[AFS_SECTOR_SIZE];
static u8 g_inode_buf[AFS_SECTOR_SIZE];
static u8 g_data_buf[AFS_BLOCK_SIZE];

/* ── Mount state ─────────────────────────────────────────────────── */
static afs_mount_t g_mount;

/* ── Open file table ─────────────────────────────────────────────── */
typedef struct {
    u8          valid;
    afs_inode_t inode;
    u32         inode_num;
    u32         position;
} afs_fd_t;

static afs_fd_t g_fds[AFS_MAX_FDS];

/* ── Utility: string compare (no stdlib) ─────────────────────────── */
static int afs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static u32 afs_strlen(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static void afs_memcpy(void *dst, const void *src, u32 n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
}

static void afs_memset(void *dst, u8 val, u32 n) {
    u8 *d = (u8 *)dst;
    while (n--) *d++ = val;
}

/* ── Low-level disk I/O ──────────────────────────────────────────── */
static int afs_read_sector(u32 lba, u8 *buf) {
    return ata_read(g_mount.disk_bus, g_mount.disk_drive, lba, 1, buf);
}

/* Read a full 4096-byte block (8 sectors) */
static int afs_read_block(u32 block_num, u8 *buf) {
    u32 lba = g_mount.partition_lba
            + g_mount.sb.data_start_block * AFS_SECTORS_PER_BLOCK
            + block_num * AFS_SECTORS_PER_BLOCK;
    for (u32 i = 0; i < AFS_SECTORS_PER_BLOCK; i++) {
        if (afs_read_sector(lba + i, buf + i * AFS_SECTOR_SIZE) != 0)
            return -1;
    }
    return 0;
}

/* ── CRC32 (for GPT header validation) ──────────────────────────── */
static u32 afs_crc32(const u8 *data, u32 len) {
    u32 crc = 0xFFFFFFFFU;
    for (u32 i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320U : 0);
    }
    return ~crc;
}

/* ── Inode I/O ───────────────────────────────────────────────────── */
/*
 * Each 4096-byte block holds 32 inodes (128 bytes each).
 * Inode numbers start at 1 (root = AFS_ROOT_INODE).
 */
static int afs_read_inode(u32 inode_num, afs_inode_t *out) {
    if (inode_num == 0 || inode_num > g_mount.sb.inodes_total) {
        printk(T, "AFS: inode %u out of range\n", inode_num);
        return -1;
    }

    u32 idx        = inode_num - 1;
    u32 inodes_per_sector = AFS_SECTOR_SIZE / sizeof(afs_inode_t);
    u32 sector_off = idx / inodes_per_sector;
    u32 slot       = idx % inodes_per_sector;

    u32 inode_start_lba = g_mount.partition_lba
                        + g_mount.sb.inode_table_block * AFS_SECTORS_PER_BLOCK;

    if (afs_read_sector(inode_start_lba + sector_off, g_inode_buf) != 0) {
        printk(T, "AFS: failed to read inode sector for inode %u\n", inode_num);
        return -1;
    }

    afs_memcpy(out, g_inode_buf + slot * sizeof(afs_inode_t), sizeof(afs_inode_t));
    return 0;
}

/* ── Directory lookup ────────────────────────────────────────────── */
/*
 * Walk a directory inode looking for 'name'.
 * Returns the inode number of the entry, or 0 if not found.
 */
static u32 afs_dir_lookup(afs_inode_t *dir_inode, const char *name) {
    u32 name_len = afs_strlen(name);

    /* Iterate over the direct blocks of the directory */
    for (int bi = 0; bi < AFS_DIRECT_BLOCKS; bi++) {
        u32 block = dir_inode->blocks[bi];
        if (block == 0) break;

        if (afs_read_block(block, g_data_buf) != 0)
            break;

        u32 off = 0;
        while (off + sizeof(afs_dirent_t) <= AFS_BLOCK_SIZE) {
            afs_dirent_t *de = (afs_dirent_t *)(g_data_buf + off);
            if (de->rec_len == 0) break;
            if (de->inode_num != 0 && de->name_len == name_len) {
                const char *dname = (const char *)(g_data_buf + off + sizeof(afs_dirent_t));
                /* manual strncmp */
                u32 match = 1;
                for (u32 i = 0; i < name_len; i++) {
                    if (dname[i] != name[i]) { match = 0; break; }
                }
                if (match) return de->inode_num;
            }
            off += de->rec_len;
        }
    }
    return 0;
}

/* ── Path resolution ─────────────────────────────────────────────── */
/*
 * Resolve an absolute path to its inode number.
 * Only absolute paths (starting with '/') are supported.
 * Returns inode number on success, 0 on failure.
 */
static u32 afs_resolve_path(const char *path) {
    if (!path || path[0] != '/') {
        printk(T, "AFS: only absolute paths supported\n");
        return 0;
    }

    u32 current_ino = AFS_ROOT_INODE;
    afs_inode_t inode;

    /* Skip leading slash */
    const char *p = path + 1;
    if (*p == '\0') return current_ino; /* root directory */

    while (*p) {
        /* Extract next path component */
        char component[AFS_MAX_NAME + 1];
        u32  clen = 0;
        while (*p && *p != '/' && clen < AFS_MAX_NAME)
            component[clen++] = *p++;
        component[clen] = '\0';
        if (*p == '/') p++; /* skip separator */

        /* Read current directory inode */
        if (afs_read_inode(current_ino, &inode) != 0) return 0;
        if ((inode.mode & AFS_INODE_TYPE_MASK) != AFS_INODE_DIR) {
            printk(T, "AFS: not a directory (inode %u)\n", current_ino);
            return 0;
        }

        current_ino = afs_dir_lookup(&inode, component);
        if (current_ino == 0) {
            printk(T, "AFS: '%s' not found\n", component);
            return 0;
        }
    }
    return current_ino;
}

/* ── GPT probing ─────────────────────────────────────────────────── */
/*
 * Scan GPT partition entries for an AFS partition.
 * Returns the start LBA of the first AFS partition, or 0 if not found.
 */
static u32 afs_probe_gpt(u8 bus, u8 drive) {
    /* Read LBA 1: GPT header */
    if (ata_read(bus, drive, GPT_HEADER_LBA, 1, g_sector) != 0) return 0;

    gpt_header_t *hdr = (gpt_header_t *)g_sector;
    if (hdr->signature != GPT_SIGNATURE) return 0;

    printk(T, "AFS: GPT header found on bus=%u drive=%u\n", bus, drive);

    /* AFS type GUID bytes */
    static const u8 afs_guid[16] = {
        0xA3, 0xA2, 0xA1, 0xA0,  /* P1 LE */
        0xB1, 0xB0,               /* P2 LE */
        0xC1, 0xC0,               /* P3 LE */
        0xD0, 0xD1,               /* B0, B1 */
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5
    };

    u32 entry_lba   = (u32)hdr->partition_entry_lba;
    u32 entry_count = hdr->num_partition_entries;
    u32 entry_size  = hdr->partition_entry_size;

    for (u32 i = 0; i < entry_count && i < GPT_ENTRIES_COUNT; i++) {
        u32 lba_off  = (i * entry_size) / AFS_SECTOR_SIZE;
        u32 byte_off = (i * entry_size) % AFS_SECTOR_SIZE;

        if (ata_read(bus, drive, entry_lba + lba_off, 1, g_sector) != 0)
            continue;

        gpt_entry_t *e = (gpt_entry_t *)(g_sector + byte_off);
        if (e->start_lba == 0 && e->end_lba == 0) continue;

        /* Compare type GUID */
        u32 match = 1;
        for (int b = 0; b < 16; b++) {
            if (e->type_guid[b] != afs_guid[b]) { match = 0; break; }
        }
        if (match) {
            printk(T, "AFS: GPT AFS partition found, start_lba=%u\n",
                   (u32)e->start_lba);
            return (u32)e->start_lba;
        }
    }
    return 0;
}

/* ── MBR probing ─────────────────────────────────────────────────── */
/*
 * Scan MBR partition table for an AFS partition (type 0xAF).
 * Returns the start LBA of the first AFS partition, or 0 if not found.
 */
static u32 afs_probe_mbr(u8 bus, u8 drive) {
    if (ata_read(bus, drive, 0, 1, g_sector) != 0) return 0;
    if (g_sector[510] != 0x55 || g_sector[511] != 0xAA) return 0;

    /* Check for GPT protective MBR (type 0xEE) — skip if so */
    for (int p = 0; p < 4; p++) {
        u8 *entry = g_sector + 446 + p * 16;
        if (entry[4] == 0xEE) return 0; /* GPT disk */
    }

    printk(T, "AFS: MBR found on bus=%u drive=%u\n", bus, drive);

    for (int p = 0; p < 4; p++) {
        u8 *entry = g_sector + 446 + p * 16;
        u8  type  = entry[4];
        u32 lba   = (u32)entry[8]  | ((u32)entry[9]  << 8)
                  | ((u32)entry[10] << 16) | ((u32)entry[11] << 24);
        u32 size  = (u32)entry[12] | ((u32)entry[13] << 8)
                  | ((u32)entry[14] << 16) | ((u32)entry[15] << 24);

        if (type == 0 || size == 0) continue;

        if (type == AFS_PARTITION_TYPE_MBR) {
            printk(T, "AFS: MBR AFS partition found (part %d), lba=%u\n",
                   p, lba);
            return lba;
        }
    }
    return 0;
}

/* ── Superblock validation ───────────────────────────────────────── */
static int afs_validate_superblock(const afs_superblock_t *sb) {
    if (sb->magic != AFS_MAGIC) {
        printk(T, "AFS: bad magic 0x%x (expected 0x%x)\n",
               sb->magic, AFS_MAGIC);
        return -1;
    }
    if (sb->version != AFS_VERSION) {
        printk(T, "AFS: unsupported version %u\n", sb->version);
        return -1;
    }
    if (sb->block_size != AFS_BLOCK_SIZE) {
        printk(T, "AFS: unsupported block size %u\n", sb->block_size);
        return -1;
    }
    /* disk_size_bytes is informational only — not validated on mount */
    return 0;
}

/* ════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════ */

void afs_init(void) {
    afs_memset(&g_mount, 0, sizeof(g_mount));
    for (int i = 0; i < AFS_MAX_FDS; i++)
        g_fds[i].valid = 0;
    printk(T, "AFS: Ark File System driver initialized\n");
}

int afs_mount(const char *device) {
    (void)device; /* device string is advisory; we use disk_bus/drive */

    if (g_mount.mounted) {
        printk(T, "AFS: already mounted\n");
        return 0;
    }

    printk(T, "AFS: scanning for AFS partition...\n");

    static const u8 buses[]  = {0, 0, 1, 1};
    static const u8 drives[] = {0, 1, 0, 1};

    for (int i = 0; i < 4; i++) {
        u8  bus   = buses[i];
        u8  drive = drives[i];
        u32 part_lba = 0;
        u32 part_type = 0;

        /* Probe GPT first (preferred) */
        part_lba = afs_probe_gpt(bus, drive);
        if (part_lba) {
            part_type = 1; /* GPT */
        } else {
            /* Fall back to MBR */
            part_lba = afs_probe_mbr(bus, drive);
            if (part_lba) part_type = 0; /* MBR */
        }

        if (!part_lba) continue;

        /* Read the superblock (first sector of partition) */
        if (ata_read(bus, drive, part_lba, 1, g_sector) != 0) {
            printk(T, "AFS: failed to read superblock\n");
            continue;
        }

        afs_superblock_t *sb = (afs_superblock_t *)g_sector;
        if (afs_validate_superblock(sb) != 0) continue;

        /* Cache mount state */
        g_mount.disk_bus      = bus;
        g_mount.disk_drive    = drive;
        g_mount.partition_lba = part_lba;
        g_mount.part_type     = part_type;
        afs_memcpy(&g_mount.sb, sb, sizeof(afs_superblock_t));
        g_mount.mounted       = 1;

        printk(T, "AFS: mounted on bus=%u drive=%u lba=%u (%s)\n",
               bus, drive, part_lba, part_type ? "GPT" : "MBR");
        printk(T, "AFS: label=\"%s\" blocks=%u inodes=%u\n",
               g_mount.sb.label,
               g_mount.sb.blocks_total,
               g_mount.sb.inodes_total);
        return 0;
    }

    printk(T, "AFS: no AFS partition found\n");
    return -1;
}

int afs_open(const char *path) {
    if (!g_mount.mounted) {
        printk(T, "AFS: not mounted\n");
        return -1;
    }

    u32 ino = afs_resolve_path(path);
    if (ino == 0) {
        printk(T, "AFS: open: '%s' not found\n", path);
        return -1;
    }

    afs_inode_t inode;
    if (afs_read_inode(ino, &inode) != 0) return -1;

    if ((inode.mode & AFS_INODE_TYPE_MASK) == AFS_INODE_DIR) {
        printk(T, "AFS: open: '%s' is a directory\n", path);
        return -1;
    }

    /* Find a free file descriptor */
    for (int fd = 0; fd < AFS_MAX_FDS; fd++) {
        if (!g_fds[fd].valid) {
            g_fds[fd].valid     = 1;
            g_fds[fd].inode_num = ino;
            g_fds[fd].position  = 0;
            afs_memcpy(&g_fds[fd].inode, &inode, sizeof(afs_inode_t));
            printk(T, "AFS: opened '%s' (inode=%u size=%u) -> fd=%d\n",
                   path, ino, (u32)inode.size, fd);
            return fd;
        }
    }

    printk(T, "AFS: too many open files\n");
    return -1;
}

int afs_read(int fd, void *buffer, u32 size) {
    if (fd < 0 || fd >= AFS_MAX_FDS || !g_fds[fd].valid) {
        printk(T, "AFS: read: bad fd %d\n", fd);
        return -1;
    }

    afs_fd_t    *f    = &g_fds[fd];
    afs_inode_t *ino  = &f->inode;
    u8          *out  = (u8 *)buffer;
    u32          pos  = f->position;
    u32          total = (u32)ino->size;
    u32          read_bytes = 0;

    if (pos >= total) return 0;
    if (pos + size > total) size = total - pos;

    while (size > 0) {
        u32 block_idx  = pos / AFS_BLOCK_SIZE;
        u32 block_off  = pos % AFS_BLOCK_SIZE;
        u32 can_read   = AFS_BLOCK_SIZE - block_off;
        if (can_read > size) can_read = size;

        /* Get the physical block number */
        u32 phys_block = 0;

        if (block_idx < AFS_DIRECT_BLOCKS) {
            phys_block = ino->blocks[block_idx];
        } else if (ino->indirect) {
            /* Single-indirect */
            u32 indirect_idx = block_idx - AFS_DIRECT_BLOCKS;
            if (indirect_idx < (AFS_BLOCK_SIZE / 4)) {
                if (afs_read_block(ino->indirect, g_data_buf) != 0) break;
                u32 *tbl = (u32 *)g_data_buf;
                phys_block = tbl[indirect_idx];
            }
        }
        /* (Double-indirect omitted for initial implementation) */

        if (phys_block == 0) break;

        if (afs_read_block(phys_block, g_data_buf) != 0) break;

        afs_memcpy(out, g_data_buf + block_off, can_read);
        out        += can_read;
        pos        += can_read;
        size       -= can_read;
        read_bytes += can_read;
    }

    f->position = pos;
    return (int)read_bytes;
}

int afs_close(int fd) {
    if (fd < 0 || fd >= AFS_MAX_FDS || !g_fds[fd].valid) {
        printk(T, "AFS: close: bad fd %d\n", fd);
        return -1;
    }
    g_fds[fd].valid = 0;
    return 0;
}

u32 afs_file_size(int fd) {
    if (fd < 0 || fd >= AFS_MAX_FDS || !g_fds[fd].valid) return 0;
    return (u32)g_fds[fd].inode.size;
}

u8 afs_is_mounted(void) {
    return g_mount.mounted;
}

void afs_dump_superblock(void) {
    if (!g_mount.mounted) {
        printk(T, "AFS: not mounted\n");
        return;
    }
    afs_superblock_t *sb = &g_mount.sb;
    printk(T, "AFS Superblock:\n");
    printk(T, "  magic        : 0x%x\n",  sb->magic);
    printk(T, "  version      : %u\n",    sb->version);
    printk(T, "  block_size   : %u\n",    sb->block_size);
    printk(T, "  blocks_total : %u\n",    sb->blocks_total);
    printk(T, "  blocks_free  : %u\n",    sb->blocks_free);
    printk(T, "  inodes_total : %u\n",    sb->inodes_total);
    printk(T, "  inodes_free  : %u\n",    sb->inodes_free);
    printk(T, "  root_inode   : %u\n",    sb->root_inode);
    printk(T, "  part_type    : %s\n",    g_mount.part_type ? "GPT" : "MBR");
    printk(T, "  label        : %s\n",    sb->label);
}

/* ── afs_disk_probe: scan all ATA drives, load /init if found ─────── */
int afs_disk_probe(void) {
    if (afs_mount(0) != 0) return 0;

    int fd = afs_open("/init");
    if (fd < 0) {
        printk(T, "AFS: /init not found on AFS partition\n");
        return 0;
    }

    u32 size = afs_file_size(fd);
    if (size == 0) {
        afs_close(fd);
        return 0;
    }

    /* Use a static buffer — 256 KB max for /init */
    #define AFS_INIT_BUF (256 * 1024)
    static u8 afs_init_buf[AFS_INIT_BUF];

    u32 to_read = size < AFS_INIT_BUF ? size : AFS_INIT_BUF;
    int got = afs_read(fd, afs_init_buf, to_read);
    afs_close(fd);

    if (got <= 0) {
        printk(T, "AFS: failed to read /init\n");
        return 0;
    }

    printk(T, "AFS: loaded /init (%u bytes) from AFS partition\n", (u32)got);
    ramfs_add_file("/init", afs_init_buf, (u32)got);
    return 1;
}
