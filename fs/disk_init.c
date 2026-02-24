/**
 * disk_init.c - Load .init script from any attached disk image into ramfs.
 *
 * Boot sequence:
 *   1. Scan ATA bus 0+1, drives 0+1 (all 4 combinations)
 *   2. For each detected disk, try to read sector 0 (MBR or raw)
 *   3. If MBR: walk partition table, find first FAT16/FAT32 partition
 *   4. Read FAT boot sector, parse directory, look for "INIT    " (8.3)
 *      OR fall back to scanning the first cluster for a .init file
 *   5. Also handle raw disks (no MBR) — scan sector 0 directly as FAT
 *   6. Load .init contents into ramfs as "/init"
 *
 * The .init file format is the same as the ramfs init executor:
 *   - Plain text lines: executed as commands (echo, /path/to/elf, ...)
 *   - #! shebang: run interpreter from ramfs
 *   - ELF: executed directly
 *
 * QEMU usage:
 *   make run QEMU_EXTRA="-drive file=test.img,format=raw,if=ide"
 *   OR: make run-with-disk DISK=test.img
 */

#include "ark/types.h"
#include "ark/ata.h"
#include "ark/ramfs.h"
#include "ark/printk.h"

/* ── Sector buffer ─────────────────────────────────────────────── */
#define SECTOR_SIZE 512

/* Static sector buffer — no heap needed */
static u8 g_sector[SECTOR_SIZE];
static u8 g_fat_sector[SECTOR_SIZE];   /* one cached FAT sector */
static u8 g_dir_sector[SECTOR_SIZE];   /* directory sector */

/* ── Disk I/O helpers ──────────────────────────────────────────── */
static u8 g_disk_bus   = 0;
static u8 g_disk_drive = 0;

static int disk_read(u32 lba, u8 *buf) {
    return ata_read(g_disk_bus, g_disk_drive, lba, 1, buf);
}

/* ── Little-endian readers ─────────────────────────────────────── */
static u16 le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }

/* ── FAT state ─────────────────────────────────────────────────── */
static u32 g_fat_lba;
static u32 g_data_lba;
static u32 g_root_cluster;   /* FAT32: first cluster of root dir; FAT16: 0 */
static u32 g_root_lba;       /* FAT16 root dir LBA */
static u32 g_root_sectors;   /* FAT16 root dir sector count */
static u32 g_sec_per_clust;
static u8  g_is_fat32;

/* ── Load file data into a static heap ─────────────────────────── */
/* 256 KB ought to be enough for an .init script */
#define INIT_BUF_SIZE (256 * 1024)
static u8 g_init_buf[INIT_BUF_SIZE];

/* ── FAT32: next cluster ───────────────────────────────────────── */
static u32 fat32_next_cluster(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = g_fat_lba + fat_offset / SECTOR_SIZE;
    u32 fat_off    = fat_offset % SECTOR_SIZE;
    if (disk_read(fat_sector, g_fat_sector) != 0) return 0x0FFFFFF8;
    return le32(g_fat_sector + fat_off) & 0x0FFFFFFF;
}

/* ── FAT16: next cluster ───────────────────────────────────────── */
static u16 fat16_next_cluster(u16 cluster) {
    u32 fat_offset = (u32)cluster * 2;
    u32 fat_sector = g_fat_lba + fat_offset / SECTOR_SIZE;
    u32 fat_off    = fat_offset % SECTOR_SIZE;
    if (disk_read(fat_sector, g_fat_sector) != 0) return 0xFFFF;
    return le16(g_fat_sector + fat_off);
}

/* ── Cluster → LBA ─────────────────────────────────────────────── */
static u32 cluster_lba(u32 cluster) {
    return g_data_lba + (cluster - 2) * g_sec_per_clust;
}

/* ── Read a full cluster chain into g_init_buf ─────────────────── */
static u32 read_cluster_chain(u32 start_cluster) {
    u32 total = 0;
    u32 cluster = start_cluster;

    while (1) {
        /* End of chain markers */
        if (g_is_fat32 && cluster >= 0x0FFFFFF8) break;
        if (!g_is_fat32 && cluster >= 0xFFF8) break;
        if (cluster < 2) break;

        u32 lba = cluster_lba(cluster);
        for (u32 s = 0; s < g_sec_per_clust; s++) {
            if (total + SECTOR_SIZE > INIT_BUF_SIZE) goto done;
            if (disk_read(lba + s, g_init_buf + total) != 0) goto done;
            total += SECTOR_SIZE;
        }

        if (g_is_fat32)
            cluster = fat32_next_cluster(cluster);
        else
            cluster = (u32)fat16_next_cluster((u16)cluster);
    }
done:
    return total;
}

/* ── Compare 8.3 directory entry name (space-padded) ──────────── */
/* name83 must be exactly 11 bytes */
static int match83(const u8 *entry, const char *name83) {
    for (int i = 0; i < 11; i++) {
        u8 a = entry[i];
        u8 b = (u8)name83[i];
        /* uppercase compare */
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

/* ── Scan one directory sector for .init ──────────────────────── */
/* Returns start cluster if found, 0 if not */
static u32 scan_dir_sector(const u8 *sec) {
    for (int i = 0; i < 512; i += 32) {
        const u8 *e = sec + i;
        if (e[0] == 0x00) break;        /* end of directory */
        if (e[0] == 0xE5) continue;     /* deleted */
        if (e[11] & 0x08) continue;     /* volume label */
        if (e[11] & 0x10) continue;     /* subdirectory */

        /* Match: ".INIT      " (8+3, space padded, no dot stored) */
        /* File named ".init" → 8.3 is ".INIT   " — tricky.
         * Filenames without extension: "INIT    " + "   "
         * With extension: "INIT    " + "SCR" etc.
         * We accept any of: INIT, .INIT, INIT.SH, INIT.TXT, INIT.SCR */
        u8 name[8], ext[3];
        for (int j=0;j<8;j++) name[j]=e[j];
        for (int j=0;j<3;j++) ext[j]=e[8+j];

        /* base name must be "INIT    " or ".INIT   " */
        int name_match = 0;
        /* "INIT    " */
        if (name[0]=='I'&&name[1]=='N'&&name[2]=='I'&&name[3]=='T') {
            int rest_blank = 1;
            for (int j=4;j<8;j++) if(name[j]!=' ') { rest_blank=0; break; }
            if (rest_blank) name_match = 1;
        }
        /* ".INIT   " — dot as first char */
        if (name[0]=='.'&&name[1]=='I'&&name[2]=='N'&&name[3]=='I'&&name[4]=='T') {
            int rest_blank = 1;
            for (int j=5;j<8;j++) if(name[j]!=' ') { rest_blank=0; break; }
            if (rest_blank) name_match = 1;
        }

        if (!name_match) continue;

        /* Extension: blank, SH, TXT, SCR, INI — accept anything */
        (void)ext;   /* all extensions accepted */

        u32 cluster_hi = (u32)le16(e + 20) << 16;
        u32 cluster_lo = (u32)le16(e + 26);
        u32 cluster = cluster_hi | cluster_lo;
        if (!g_is_fat32) cluster &= 0xFFFF;

        printk(T,"disk: found INIT file, cluster=%u size=%u\n",
               cluster, le32(e + 28));
        return cluster ? cluster : 1;
    }
    return 0;
}

/* ── Scan FAT16 root dir (fixed location) ──────────────────────── */
static u32 scan_fat16_root(void) {
    for (u32 s = 0; s < g_root_sectors; s++) {
        if (disk_read(g_root_lba + s, g_dir_sector) != 0) continue;
        u32 c = scan_dir_sector(g_dir_sector);
        if (c) return c;
    }
    return 0;
}

/* ── Scan FAT32 root dir (cluster chain) ───────────────────────── */
static u32 scan_fat32_root(void) {
    u32 cluster = g_root_cluster;
    while (cluster < 0x0FFFFFF8 && cluster >= 2) {
        u32 lba = cluster_lba(cluster);
        for (u32 s = 0; s < g_sec_per_clust; s++) {
            if (disk_read(lba + s, g_dir_sector) != 0) continue;
            u32 c = scan_dir_sector(g_dir_sector);
            if (c) return c;
        }
        cluster = fat32_next_cluster(cluster);
    }
    return 0;
}

/* ── Try to parse a FAT boot sector and load .init ─────────────── */
/* Returns 1 if .init was loaded into ramfs */
static int try_fat(u32 partition_lba) {
    if (disk_read(partition_lba, g_sector) != 0) return 0;

    u8 *bs = g_sector;

    /* Quick sanity: bytes per sector must be 512 */
    u16 bps = le16(bs + 11);
    if (bps != 512) return 0;

    u8  spc      = bs[13];
    u16 rsvd     = le16(bs + 14);
    u8  num_fats = bs[16];
    u16 root_ent = le16(bs + 17);   /* 0 for FAT32 */

    if (spc == 0 || num_fats == 0) return 0;

    u32 fat_size;
    u16 fat16_size = le16(bs + 22);
    if (fat16_size != 0)
        fat_size = fat16_size;
    else
        fat_size = le32(bs + 36);   /* FAT32 BPB_FATSz32 */

    g_fat_lba      = partition_lba + rsvd;
    g_sec_per_clust = spc;
    g_is_fat32     = (root_ent == 0);

    if (g_is_fat32) {
        g_root_cluster = le32(bs + 44);
        g_data_lba     = g_fat_lba + num_fats * fat_size;
        g_root_lba     = 0;
        g_root_sectors = 0;
        printk(T,"disk: FAT32 fat_lba=%u data_lba=%u root_clust=%u\n",
               g_fat_lba, g_data_lba, g_root_cluster);
    } else {
        g_root_lba     = g_fat_lba + num_fats * fat_size;
        g_root_sectors = (root_ent * 32 + 511) / 512;
        g_data_lba     = g_root_lba + g_root_sectors;
        g_root_cluster = 0;
        printk(T,"disk: FAT16 fat_lba=%u root_lba=%u data_lba=%u\n",
               g_fat_lba, g_root_lba, g_data_lba);
    }

    /* Find .init entry */
    u32 init_cluster = g_is_fat32 ? scan_fat32_root() : scan_fat16_root();
    if (!init_cluster) {
        printk(T,"disk: no .init / INIT file found on partition at lba=%u\n",
               partition_lba);
        return 0;
    }

    /* Read data */
    u32 bytes = read_cluster_chain(init_cluster);
    if (bytes == 0) {
        printk(T,"disk: failed to read .init data\n");
        return 0;
    }

    /* Trim to real file size if we know it (already in g_init_buf) */
    /* Find a reasonable end: trim trailing null bytes */
    while (bytes > 1 && g_init_buf[bytes-1] == 0) bytes--;

    printk(T,"disk: loaded .init (%u bytes) -> /init\n", bytes);
    ramfs_add_file("/init", g_init_buf, bytes);
    return 1;
}

/* ── Walk MBR partition table ──────────────────────────────────── */
static int try_mbr(void) {
    if (disk_read(0, g_sector) != 0) return 0;

    /* MBR signature */
    if (g_sector[510] != 0x55 || g_sector[511] != 0xAA) {
        /* No MBR — try treating sector 0 as a raw FAT boot sector */
        printk(T,"disk: no MBR signature, trying raw FAT\n");
        return try_fat(0);
    }

    printk(T,"disk: MBR found, scanning partitions\n");

    /* 4 primary partitions at offset 446 */
    for (int p = 0; p < 4; p++) {
        u8 *entry = g_sector + 446 + p * 16;
        u8  type  = entry[4];
        u32 lba   = le32(entry + 8);
        u32 size  = le32(entry + 12);

        if (type == 0 || size == 0) continue;

        /* FAT types: 0x01=FAT12, 0x04=FAT16<32M, 0x06=FAT16,
         *            0x0B=FAT32 CHS, 0x0C=FAT32 LBA,
         *            0x0E=FAT16 LBA, 0x83=Linux (try anyway) */
        printk(T,"disk: partition %d type=0x%x lba=%u\n", p, type, lba);

        int fat_types[] = {0x01,0x04,0x06,0x0B,0x0C,0x0E,0x0F,0x83,0};
        int is_fat = 0;
        for (int i = 0; fat_types[i]; i++)
            if (type == fat_types[i]) { is_fat = 1; break; }

        if (is_fat || type != 0) {   /* try all non-empty partitions */
            if (try_fat(lba)) return 1;
        }
    }
    return 0;
}

/* ── Public entry point ────────────────────────────────────────── */
/**
 * disk_load_init() - Scan all ATA disks for a .init file and load into ramfs.
 *
 * Called from kernel_main after ATA init. If a /init is already in ramfs
 * (from ZIP initramfs), we skip the disk scan — disk takes lower priority.
 *
 * Returns 1 if .init was found and loaded, 0 otherwise.
 */
int disk_load_init(void) {
    printk(T,"disk: scanning ATA buses for .init script\n");

    /* Try all 4 combinations: bus 0/1, drive 0/1 */
    static const u8 buses[]  = {0, 0, 1, 1};
    static const u8 drives[] = {0, 1, 0, 1};

    for (int i = 0; i < 4; i++) {
        g_disk_bus   = buses[i];
        g_disk_drive = drives[i];

        /* Quick probe: read sector 0 */
        if (ata_read(g_disk_bus, g_disk_drive, 0, 1, g_sector) != 0)
            continue;

        printk(T,"disk: found drive bus=%u drive=%u\n", g_disk_bus, g_disk_drive);

        if (try_mbr()) {
            printk(T,"disk: .init loaded from bus=%u drive=%u\n",
                   g_disk_bus, g_disk_drive);
            return 1;
        }
    }

    printk(T,"disk: no .init found on any ATA disk\n");
    return 0;
}
