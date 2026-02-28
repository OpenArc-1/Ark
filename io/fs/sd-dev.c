/**
 * fs/sd-dev.c
 * Disk scanner for Ark kernel — protected-mode ATA PIO implementation.
 *
 * Replaces the original BIOS INT 13h version which used push/pop %%es,
 * illegal in 32-bit GCC inline asm (-m32 / -ffreestanding).
 *
 * Detection strategy (all done via I/O ports, no BIOS calls):
 *   1. Identify drives on ATA bus 0 (primary) and bus 1 (secondary),
 *      master and slave — up to 4 drives total.
 *   2. For each present drive, send ATA IDENTIFY DEVICE (0xEC).
 *      Words 60-61 give 28-bit LBA sector count (ATA-1..ATA-5).
 *      Words 100-103 give 48-bit LBA sector count (ATA-6+).
 *   3. Convert sector count → MB/GB and store in DiskInfo.
 *
 * This mirrors the information the old INT 13h code tried to obtain.
 */

#include "ark/types.h"
#include "ark/printk.h"

/* ── ATA I/O port bases ──────────────────────────────────────────────────── */
#define ATA_BUS0_BASE   0x1F0
#define ATA_BUS0_CTRL   0x3F6
#define ATA_BUS1_BASE   0x170
#define ATA_BUS1_CTRL   0x376

/* Register offsets from base */
#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA0        3
#define ATA_REG_LBA1        4
#define ATA_REG_LBA2        5
#define ATA_REG_DRIVE       6
#define ATA_REG_STATUS      7
#define ATA_REG_COMMAND     7

/* Status register bits */
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DRQ   0x08
#define ATA_SR_ERR   0x01

/* Drive/head register values */
#define ATA_DRIVE_MASTER  0xA0
#define ATA_DRIVE_SLAVE   0xB0

/* Commands */
#define ATA_CMD_IDENTIFY  0xEC

/* ── Public result type ──────────────────────────────────────────────────── */

typedef struct {
    u8   drive;           /* logical index 0-3                              */
    u32  sectors_lo;      /* low  32 bits of total LBA sector count         */
    u32  sectors_hi;      /* high 32 bits of total LBA sector count         */
    u32  total_mb;
    u32  total_gb_int;
    u32  total_gb_frac;   /* tenths: 0–9                                    */
    u8   method;          /* 1 = LBA48 (48-bit), 2 = LBA28 (28-bit)        */
    u8   valid;
} DiskInfo;

/* ── I/O helpers (no inline asm segment register tricks needed) ──────────── */

static inline u8 inb_port(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb_port(u16 port, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline u16 inw_port(u16 port) {
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void io_wait(void) {
    /* 4 dummy reads of port 0x80 = ~1 µs delay */
    inb_port(0x80); inb_port(0x80);
    inb_port(0x80); inb_port(0x80);
}

/* ── Wait helpers ────────────────────────────────────────────────────────── */

/* Returns 0 on success, -1 on timeout or error */
static int ata_wait_bsy_clear(u16 base) {
    for (int i = 0; i < 100000; i++) {
        u8 s = inb_port(base + ATA_REG_STATUS);
        if (!(s & ATA_SR_BSY)) return 0;
        io_wait();
    }
    return -1;
}

static int ata_wait_drq(u16 base) {
    for (int i = 0; i < 100000; i++) {
        u8 s = inb_port(base + ATA_REG_STATUS);
        if (s & ATA_SR_ERR)  return -1;
        if (s & ATA_SR_DRQ)  return 0;
        io_wait();
    }
    return -1;
}

/* ── ATA IDENTIFY ────────────────────────────────────────────────────────── */

/**
 * ata_identify - run IDENTIFY DEVICE on one drive.
 * base: ATA base port (0x1F0 or 0x170)
 * slave: 0 = master, 1 = slave
 * buf: caller-provided 256-word (512-byte) buffer
 * Returns 1 if drive present and identified, 0 otherwise.
 */
static int ata_identify(u16 base, int slave, u16 *buf) {
    /* Select drive */
    outb_port(base + ATA_REG_DRIVE, slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER);
    io_wait();

    /* Zero LBA/sector-count registers to indicate IDENTIFY */
    outb_port(base + ATA_REG_SECCOUNT, 0);
    outb_port(base + ATA_REG_LBA0,     0);
    outb_port(base + ATA_REG_LBA1,     0);
    outb_port(base + ATA_REG_LBA2,     0);

    /* Send IDENTIFY command */
    outb_port(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();

    /* If status is 0 after command, drive doesn't exist */
    u8 status = inb_port(base + ATA_REG_STATUS);
    if (status == 0x00 || status == 0xFF) return 0;

    /* Wait for BSY to clear */
    if (ata_wait_bsy_clear(base) < 0) return 0;

    /* Check LBA1/LBA2 — if non-zero it's an ATAPI device (not a plain ATA disk) */
    u8 lba1 = inb_port(base + ATA_REG_LBA1);
    u8 lba2 = inb_port(base + ATA_REG_LBA2);
    if (lba1 || lba2) return 0; /* ATAPI/SATA — skip */

    /* Wait for DRQ */
    if (ata_wait_drq(base) < 0) return 0;

    /* Read 256 words from data port */
    for (int i = 0; i < 256; i++) {
        buf[i] = inw_port(base + ATA_REG_DATA);
    }
    return 1;
}

/* ── Sector-count extraction ─────────────────────────────────────────────── */

static void extract_sectors(const u16 *id,
                             u32 *lo, u32 *hi, u8 *method) {
    /* Words 100-103: 48-bit LBA sector count (ATA-6+) */
    u32 hi48 = ((u32)id[103] << 16) | id[102];
    u32 lo48 = ((u32)id[101] << 16) | id[100];

    if (hi48 || lo48) {
        *lo     = lo48;
        *hi     = hi48;
        *method = 1;   /* LBA48 */
        return;
    }

    /* Words 60-61: 28-bit LBA sector count (ATA-1..5) */
    u32 lba28 = ((u32)id[61] << 16) | id[60];
    *lo     = lba28;
    *hi     = 0;
    *method = 2;   /* LBA28 */
}

/* ── MB conversion ───────────────────────────────────────────────────────── */

static u32 sectors_to_mb(u32 lo, u32 hi) {
    /* 1 MB = 2048 × 512-byte sectors */
    if (hi >= 2048u) return 0xFFFFFFFFu;    /* >4 TB, cap */
    return (hi * 2097152u) + (lo / 2048u);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * scan_disk - identify one ATA drive by logical index 0-3.
 *   0 = bus0 master, 1 = bus0 slave, 2 = bus1 master, 3 = bus1 slave
 */
int scan_disk(u8 drive, DiskInfo *info) {
    static const u16 bases[4]  = { ATA_BUS0_BASE, ATA_BUS0_BASE,
                                    ATA_BUS1_BASE, ATA_BUS1_BASE };
    static const int  slaves[4] = { 0, 1, 0, 1 };

    if (drive >= 4) return 0;

    u16 id[256];
    if (!ata_identify(bases[drive], slaves[drive], id)) return 0;

    u32 lo = 0, hi = 0;
    u8  method = 0;
    extract_sectors(id, &lo, &hi, &method);

    /* Zero sector count = no media */
    if (!lo && !hi) return 0;

    info->drive         = drive;
    info->sectors_lo    = lo;
    info->sectors_hi    = hi;
    info->method        = method;
    info->total_mb      = sectors_to_mb(lo, hi);
    info->total_gb_int  = info->total_mb / 1024u;
    info->total_gb_frac = (info->total_mb % 1024u) * 10u / 1024u;
    info->valid         = 1;
    return 1;
}

int scan_all_disk(DiskInfo *results) {
    int count = 0;
    for (int i = 0; i < 4; i++) {
        if (scan_disk((u8)i, &results[i])) {
            count++;
        } else {
            results[i].valid = 0;
        }
    }
    return count;
}

/* ── Display helpers ─────────────────────────────────────────────────────── */

void print_disk_info(const DiskInfo *info) {
    if (!info->valid) return;

    static const char *bus_str[4]   = { "bus0", "bus0", "bus1", "bus1" };
    static const char *role_str[4]  = { "master", "slave", "master", "slave" };

    printk("  [disk%u] %s/%s: ", (u32)info->drive,
           bus_str[info->drive & 3], role_str[info->drive & 3]);

    if (info->total_gb_int > 0)
        printk("%u.%u GB ", info->total_gb_int, info->total_gb_frac);
    else
        printk("%u MB ", info->total_mb);

    printk("(%u MB) [%s]",
           info->total_mb,
           info->method == 1 ? "LBA48" : "LBA28");

    if (info->sectors_hi)
        printk(" sectors: 0x%x%08x", info->sectors_hi, info->sectors_lo);
    else
        printk(" sectors: %u", info->sectors_lo);

    printk("\n");
}

void print_disk_summary(const DiskInfo *disks, int count) {
    if (count == 0) {
        printk("[disk] No storage devices detected\n");
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (disks[i].valid)
            print_disk_info(&disks[i]);
    }
    printk("[disk] Total devices found: %d\n", count);
}

void scan_and_display_disks(void) {
    DiskInfo disks[4];
    printk("[disk] Scanning ATA storage devices...\n");
    int count = scan_all_disk(disks);
    if (count > 0)
        print_disk_summary(disks, count);
    else
        printk("[disk] No storage devices detected\n");
}

void scan_and_display_disks_simple(void) {
    DiskInfo disks[4];
    printk("[disk] Scanning storage devices...\n");
    int count = scan_all_disk(disks);
    if (count == 0) { printk("[disk] No devices found\n"); return; }
    printk("[disk] Found %d device(s):\n", count);
    for (int i = 0; i < 4; i++)
        if (disks[i].valid) print_disk_info(&disks[i]);
    printk("\n");
}
