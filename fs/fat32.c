/**
 * FAT32 Filesystem Driver for Ark
 *
 * Provides basic FAT32 filesystem support for reading files from disk.
 */

#include "ark/fat32.h"
#include "ark/printk.h"
#include "ark/mmio.h"
#include "ark/pci.h"

/* FAT32 Boot Sector Structure (simplified) */
typedef struct {
    u8  jump_boot[3];
    u8  oem_name[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  num_fats;
    u16 root_entries;
    u16 total_sectors_16;
    u8  media_descriptor;
    u16 sectors_per_fat_16;
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors_32;
    /* FAT32 specific */
    u32 sectors_per_fat;
    u16 flags;
    u16 version;
    u32 root_cluster;
    u16 fsinfo_sector;
    u16 backup_boot_sector;
} __attribute__((packed)) fat32_boot_sector_t;

static u8 fat32_initialized = 0;
/* Note: These will be used in full FAT32 implementation */
/* static u32 fat32_partition_lba = 0; */
/* static u32 fat32_sectors_per_cluster = 0; */
/* static u32 fat32_bytes_per_sector = 512; */
/* static u32 fat32_fat_lba = 0; */
/* static u32 fat32_data_lba = 0; */

void fat32_init(void) {
    printk(T,"FAT32: Initializing FAT32 filesystem...\n");
    
    /* For now, FAT32 is initialized but not actively mounted */
    /* Full implementation would:
     * 1. Scan partitions for FAT32
     * 2. Read boot sector
     * 3. Parse partition info
     * 4. Load FAT table into memory
     */
    
    fat32_initialized = 1;
    printk(T,"FAT32: Filesystem driver loaded\n");
}

int fat32_mount(const char *device) {
    if (!fat32_initialized) {
        printk(T,"Error: FAT32 not initialized\n");
        return -1;
    }
    
    printk(T,"Mounting FAT32 filesystem on %s\n", device);
    
    /* Full implementation would actually mount the device */
    /* For now, return success as a stub */
    return 0;
}

int fat32_open(const char *path) {
    printk(T,"Opening file: %s\n", path);
    return -1;  /* File not found */
}

int fat32_read(int fd, void *buffer, u32 size) {
    (void)fd;  /* Unused in stub */
    (void)buffer;  /* Unused in stub */
    (void)size;  /* Unused in stub */
    
    printk(T,"Reading %u bytes from file descriptor %d\n", size, fd);
    return -1;  /* Read failed */
}

int fat32_close(int fd) {
    printk(T,"Closing file descriptor %d\n", fd);
    return 0;
}

u32 fat32_file_size(int fd) {
    (void)fd;  /* Unused in stub */
    return 0;
}
