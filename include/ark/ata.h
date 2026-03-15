/**
 * ATA (Advanced Technology Attachment) Driver
 * 
 * Provides IDE hard drive access for legacy systems.
 */

#pragma once

#include "ark/types.h"

/**
 * ATA device types
 */
enum ata_device_type {
    ATA_TYPE_UNKNOWN = 0,
    ATA_TYPE_ATA,
    ATA_TYPE_ATAPI
};

/**
 * ATA device information
 */
typedef struct {
    u8  bus;
    u8  drive;
    u16 cylinders;
    u16 heads;
    u16 sectors;
    u32 lba_sectors;
    enum ata_device_type type;
} ata_device_t;

/**
 * Initialize ATA subsystem
 */
void ata_init(void);

/**
 * Read sector(s) from ATA device
 * @param bus Primary (0) or Secondary (1)
 * @param drive Master (0) or Slave (1)
 * @param lba Logical block address
 * @param count Number of sectors to read
 * @param buffer Buffer to read into
 * @return 0 on success, non-zero on error
 */
int ata_read(u8 bus, u8 drive, u32 lba, u32 count, void *buffer);

/**
 * Write sector(s) to ATA device
 * @param bus Primary (0) or Secondary (1)
 * @param drive Master (0) or Slave (1)
 * @param lba Logical block address
 * @param count Number of sectors to write
 * @param buffer Buffer to write from
 * @return 0 on success, non-zero on error
 */
int ata_write(u8 bus, u8 drive, u32 lba, u32 count, const void *buffer);
