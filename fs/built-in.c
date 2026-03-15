/**
 * Filesystem and Storage Subsystem Built-in initialization
 *
 * This module initializes all storage and filesystem drivers as a unified subsystem.
 */

#include "ark/ata.h"
#include "ark/sata.h"
#include "ark/ramfs.h"
#include "ark/fat32.h"
#include "ark/afs.h"
#include "ark/vfs.h"
#include "ark/printk.h"
#include "ark/kconfig.h"

extern void ata_init(void);
#if CONFIG_SATA_ENABLE
extern void sata_init(void);
#endif

void fs_storage_init(void) {
    printk(T,"Initialising storage subsystem...\n");

    /* Initialize ATA/IDE controller */
#if CONFIG_ATA_ENABLE
    ata_init();
#endif

    /* Initialize SATA/AHCI controller */
#if CONFIG_SATA_ENABLE
    sata_init();
#endif

    printk(T,"Storage subsystem initialized\n");
}

void fs_built_in_init(void) {
    printk(T,"Initializing filesystem drivers...\n");

    /* Initialize virtual filesystem layer */
    vfs_init();

    /* Initialize FAT32 driver */
    fat32_init();

    /* Initialize AFS (Ark File System) driver */
    afs_init();

    /* NOTE: ata_init() and sata_init() are NOT called here.
     * They are spawned as affinity-scheduled tasks by driver_affinity_init()
     * (ata_scan → core 3, sata_scan → core 4) and kernel_main waits for
     * both to complete via driver_affinity_wait_storage() before calling
     * disk_load_init(). */

    printk(T,"Filesystem drivers initialized\n");
}
