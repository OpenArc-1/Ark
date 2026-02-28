/**
 * Filesystem and Storage Subsystem Built-in initialization
 *
 * This module initializes all storage and filesystem drivers as a unified subsystem.
 */

#include "ark/ata.h"
#include "ark/sata.h"
#include "ark/ramfs.h"
#include "ark/fat32.h"
#include "ark/vfs.h"
#include "ark/printk.h"

extern void ata_init(void);
extern void sata_init(void);

void fs_storage_init(void) {
    printk(T,"Initialising storage subsystem...\n");
    
    /* Initialize ATA/IDE controller */
    ata_init();
    
    /* Initialize SATA/AHCI controller */
    sata_init();
    
    printk(T,"Storage subsystem initialized\n");
}

void fs_built_in_init(void) {
    printk(T,"Initializing filesystem drivers...\n");
    
    /* Initialize virtual filesystem layer */
    vfs_init();
    
    /* Initialize FAT32 driver */
    fat32_init();
    
    /* Initialize storage drivers */
    fs_storage_init();
    
    printk(T,"Filesystem drivers initialized\n");
}
