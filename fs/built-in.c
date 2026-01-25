/**
 * Filesystem and Storage Subsystem Built-in initialization
 *
 * This module initializes all storage and filesystem drivers as a unified subsystem.
 */

#include "ata.h"
#include "sata.h"
#include "ramfs.h"
#include "fat32.h"
#include "vfs.h"
#include "ark/printk.h"

extern void ata_init(void);
extern void sata_init(void);

void fs_storage_init(void) {
    printk("[    0.000160] Initialising storage subsystem...\n");
    
    /* Initialize ATA/IDE controller */
    ata_init();
    
    /* Initialize SATA/AHCI controller */
    sata_init();
    
    printk("[    0.000167] Storage subsystem initialized\n");
}

void fs_built_in_init(void) {
    printk("[    0.000150] Initializing filesystem drivers...\n");
    
    /* Initialize virtual filesystem layer */
    vfs_init();
    
    /* Initialize FAT32 driver */
    fat32_init();
    
    /* Initialize storage drivers */
    fs_storage_init();
    
    printk("[    0.000170] Filesystem drivers initialized\n");
}
