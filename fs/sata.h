/**
 * SATA (Serial ATA) Driver
 *
 * Provides Serial ATA hard drive and SSD access.
 */

#pragma once

#include "ark/types.h"

/**
 * SATA device information
 */
typedef struct {
    u8  bus;
    u8  port;
    u32 capacity;  // in sectors
    u16 model[20];
    u16 serial[10];
} sata_device_t;

/**
 * Initialize SATA subsystem
 */
void sata_init(void);

/**
 * Read sector(s) from SATA device
 * @param bus SATA controller bus
 * @param port Port number
 * @param lba Logical block address
 * @param count Number of sectors to read
 * @param buffer Buffer to read into
 * @return 0 on success, non-zero on error
 */
int sata_read(u8 bus, u8 port, u32 lba, u32 count, void *buffer);

/**
 * Write sector(s) to SATA device
 * @param bus SATA controller bus
 * @param port Port number
 * @param lba Logical block address
 * @param count Number of sectors to write
 * @param buffer Buffer to write from
 * @return 0 on success, non-zero on error
 */
int sata_write(u8 bus, u8 port, u32 lba, u32 count, const void *buffer);

/**
 * Get number of connected SATA devices
 * @return Number of devices
 */
u32 sata_device_count(void);

/**
 * Get SATA device info
 * @param index Device index
 * @param dev Output device structure
 * @return 0 on success, non-zero on error
 */
int sata_get_device(u32 index, sata_device_t *dev);
