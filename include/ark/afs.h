/**
 * AFS - Ark File System Header
 *
 * AFS is a custom filesystem designed for the Ark OS kernel.
 * It supports disks up to 512 GB and works with both GPT and MBR
 * partition tables.
 *
 * Disk layout (GPT):
 *   LBA 0         : Protective MBR
 *   LBA 1         : GPT Header
 *   LBA 2–33      : GPT Partition Entries
 *   LBA 34+       : AFS Superblock, then data
 *
 * Disk layout (MBR):
 *   LBA 0         : MBR with partition table (type 0xAF = AFS)
 *   LBA (part)+0  : AFS Superblock
 *   LBA (part)+1  : Block Allocation Bitmap
 *   LBA (part)+N  : Inode Table
 *   LBA (part)+M  : Data blocks
 *
 * AFS Superblock (at partition start, 512 bytes):
 *   Magic      : 0x41465321  ("AFS!")
 *   Version    : 1
 *   Block size : 4096 bytes
 *   Max disk   : 512 GB (2^39 bytes)
 *   Max inodes : configurable, stored in superblock
 */

#ifndef AFS_H
#define AFS_H

#include "ark/types.h"

/* ── AFS Limits ──────────────────────────────────────────────────── */
#define AFS_MAX_DISK_BYTES      (512ULL * 1024ULL * 1024ULL * 1024ULL) /* 512 GB */
#define AFS_BLOCK_SIZE          4096
#define AFS_SECTOR_SIZE         512
#define AFS_SECTORS_PER_BLOCK   (AFS_BLOCK_SIZE / AFS_SECTOR_SIZE)
#define AFS_MAX_FILENAME        255
#define AFS_MAX_PATH            4096
#define AFS_MAX_LINKS           65535
#define AFS_ROOT_INODE          1
#define AFS_MAGIC               0x41465321  /* "AFS!" */
#define AFS_VERSION             1
#define AFS_PARTITION_TYPE_MBR  0xAF        /* MBR partition type for AFS */

/* ── GPT constants ───────────────────────────────────────────────── */
#define GPT_HEADER_LBA          1
#define GPT_ENTRIES_LBA         2
#define GPT_ENTRIES_COUNT       128
#define GPT_ENTRY_SIZE          128
#define GPT_SIGNATURE           0x5452415020494645ULL  /* "EFI PART" */
#define GPT_HEADER_SIZE         92

/* AFS GPT GUID: {A0A1A2A3-B0B1-C0C1-D0D1-E0E1E2E3E4E5} */
#define AFS_GPT_GUID_P1         0xA0A1A2A3
#define AFS_GPT_GUID_P2         0xB0B1
#define AFS_GPT_GUID_P3         0xC0C1
#define AFS_GPT_GUID_B0         0xD0
#define AFS_GPT_GUID_B1         0xD1

/* ── Inode flags ─────────────────────────────────────────────────── */
#define AFS_INODE_DIR           0x4000
#define AFS_INODE_REG           0x8000
#define AFS_INODE_SYMLINK       0xA000
#define AFS_INODE_TYPE_MASK     0xF000

/* ── Direct/indirect block pointers per inode ────────────────────── */
#define AFS_DIRECT_BLOCKS       12
#define AFS_INDIRECT_BLOCK      12
#define AFS_DOUBLE_INDIRECT     13

/* ── On-disk structures ──────────────────────────────────────────── */

/**
 * AFS Superblock — first block of every AFS partition.
 * Total size: 512 bytes (fits in one sector).
 */
typedef struct {
    u32 magic;              /* AFS_MAGIC = 0x41465321 */
    u32 version;            /* AFS_VERSION = 1 */
    u32 block_size;         /* always AFS_BLOCK_SIZE (4096) */
    u32 blocks_total;       /* total data blocks on partition */
    u32 blocks_free;        /* free data blocks */
    u32 inodes_total;       /* total inodes */
    u32 inodes_free;        /* free inodes */
    u32 inode_table_block;  /* block# where inode table starts */
    u32 bitmap_block;       /* block# where block bitmap starts */
    u32 data_start_block;   /* block# where data area starts */
    u32 root_inode;         /* always AFS_ROOT_INODE */
    u32 partition_type;     /* 0 = MBR, 1 = GPT */
    u64 disk_size_bytes;    /* total disk size (≤ 512 GB) */
    u32 created_time;       /* creation timestamp (Unix epoch) */
    u32 last_mount_time;    /* last mount timestamp */
    u32 mount_count;        /* number of times mounted */
    u32 flags;              /* feature flags (reserved, 0 for now) */
    u8  uuid[16];           /* filesystem UUID */
    u8  label[32];          /* volume label, null-terminated */
    u8  _reserved[368];     /* padding to 512 bytes */
} __attribute__((packed)) afs_superblock_t;

/**
 * AFS Inode — describes a single file or directory.
 * Size: 128 bytes — 32 inodes per 4096-byte block.
 */
typedef struct {
    u16 mode;               /* file type + permissions (AFS_INODE_*) */
    u16 links;              /* hard link count */
    u32 uid;                /* owner UID */
    u32 gid;                /* owner GID */
    u32 flags;              /* inode flags */
    u64 size;               /* file size in bytes */
    u32 atime;              /* last access time */
    u32 mtime;              /* last modification time */
    u32 ctime;              /* inode change time */
    u32 blocks[AFS_DIRECT_BLOCKS]; /* direct block pointers */
    u32 indirect;           /* single-indirect block pointer */
    u32 dindirect;          /* double-indirect block pointer */
    u32 inode_num;          /* this inode's number */
    u8  _reserved[12];      /* padding to 128 bytes */
} __attribute__((packed)) afs_inode_t;

/**
 * AFS Directory Entry — variable length, name null-terminated.
 * Fixed part is 8 bytes; name follows immediately.
 */
typedef struct {
    u32 inode_num;          /* inode number (0 = unused) */
    u8  name_len;           /* length of name in bytes */
    u8  file_type;          /* 0=unknown, 1=regular, 2=directory, 7=symlink */
    u16 rec_len;            /* total record length (must be 4-byte aligned) */
    /* char name[name_len]; follows here */
} __attribute__((packed)) afs_dirent_t;

/**
 * GPT Header (LBA 1) — 92 bytes of relevant data.
 */
typedef struct {
    u64 signature;          /* GPT_SIGNATURE */
    u32 revision;           /* 0x00010000 */
    u32 header_size;        /* 92 */
    u32 header_crc32;
    u32 _reserved;
    u64 my_lba;             /* LBA of this header */
    u64 alt_lba;            /* LBA of backup header */
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8  disk_guid[16];
    u64 partition_entry_lba;
    u32 num_partition_entries;
    u32 partition_entry_size;
    u32 partition_array_crc32;
} __attribute__((packed)) gpt_header_t;

/**
 * GPT Partition Entry — 128 bytes each.
 */
typedef struct {
    u8  type_guid[16];
    u8  part_guid[16];
    u64 start_lba;
    u64 end_lba;
    u64 attributes;
    u16 name[36];           /* UTF-16LE partition name */
} __attribute__((packed)) gpt_entry_t;

/* ── In-memory AFS mount state ───────────────────────────────────── */
typedef struct {
    u8            mounted;
    u8            disk_bus;
    u8            disk_drive;
    u32           partition_lba;    /* LBA of partition start */
    afs_superblock_t sb;           /* cached superblock */
    u32           part_type;       /* 0 = MBR, 1 = GPT */
} afs_mount_t;

/* ── Public API ──────────────────────────────────────────────────── */

/** Initialise the AFS driver (call once at boot). */
void afs_init(void);

/**
 * Mount an AFS partition.
 * @param device  e.g. "/dev/sda1" (ignored for now; disk_bus/drive used)
 * @return 0 on success, negative on error
 */
int afs_mount(const char *device);

/** Open a file by absolute path. Returns fd ≥ 0 or -1 on error. */
int afs_open(const char *path);

/** Read up to size bytes from fd into buffer. */
int afs_read(int fd, void *buffer, u32 size);

/** Close a file descriptor. */
int afs_close(int fd);

/** Return file size for an open fd. */
u32 afs_file_size(int fd);

/**
 * Probe all ATA drives for AFS (MBR or GPT).
 * Loads /init from the first AFS partition found.
 * Returns 1 if /init was loaded, 0 otherwise.
 */
int afs_disk_probe(void);

/** Return 1 if AFS is currently mounted, 0 otherwise. */
u8 afs_is_mounted(void);

/** Print AFS superblock info to kernel log. */
void afs_dump_superblock(void);

#endif /* AFS_H */
