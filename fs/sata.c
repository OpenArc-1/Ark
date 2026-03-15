/**
 * SATA (Serial ATA) Driver
 *
 * Implements Serial ATA (AHCI) hard drive and SSD access.
 */

#include "ark/sata.h"
#include "ark/printk.h"
#include "ark/pci.h"
#include "ark/mmio.h"
#include "ark/arch.h"

/* simple human-readable size formatter (no stdio) */
static void fmt_size(u32 bytes, char *buf, u32 buflen) {
    u32 v;
    const char *unit;
    if (bytes >= 1024U*1024U*1024U) {
        v = bytes / (1024U*1024U*1024U);
        unit = "GB";
    } else if (bytes >= 1024U*1024U) {
        v = bytes / (1024U*1024U);
        unit = "MB";
    } else if (bytes >= 1024U) {
        v = bytes / 1024U;
        unit = "KB";
    } else {
        v = bytes;
        unit = "bytes";
    }
    char numbuf[16];
    int nd = 0;
    if (v == 0) {
        numbuf[nd++] = '0';
    } else {
        while (v > 0 && nd < (int)sizeof(numbuf)-1) {
            numbuf[nd++] = '0' + (v % 10);
            v /= 10;
        }
    }
    for (int i = 0; i < nd/2; i++) {
        char t = numbuf[i]; numbuf[i] = numbuf[nd-1-i]; numbuf[nd-1-i] = t;
    }
    numbuf[nd] = '\0';
    int pos = 0;
    for (int i = 0; numbuf[i] && pos+1 < (int)buflen; i++) buf[pos++] = numbuf[i];
    if (pos+1 < (int)buflen) buf[pos++] = ' ';
    for (int i = 0; unit[i] && pos+1 < (int)buflen; i++) buf[pos++] = unit[i];
    buf[pos] = '\0';
}

/* AHCI Controller Registers */
#define AHCI_CAP          0x00
#define AHCI_GHC          0x04
#define AHCI_IS           0x08
#define AHCI_PI           0x0C
#define AHCI_VS           0x10
#define AHCI_CCC_CTL      0x14
#define AHCI_CCC_PORTS    0x18
#define AHCI_EM_LOC       0x1C
#define AHCI_EM_CTL       0x20
#define AHCI_CAP2         0x24

#define AHCI_PORT_BASE    0x100
#define AHCI_PORT_SIZE    0x80

static u32 sata_controller_count = 0;
static volatile void *ahci_base = NULL;

void sata_init(void) {
    printk(T,"Probing SATA controllers (AHCI)...\n");
    
    sata_controller_count = 0;
    
    /* Scan PCI for AHCI controllers */
    pci_device_t dev;
    int found = 0;
    
    for_each_pci_device(dev) {
        /* Look for SATA controller (class 0x01, subclass 0x06) */
        if (dev.class == 0x01 && dev.subclass == 0x06) {
            printk(T,"sata-ahci: Found SATA controller %04x:%04x at %d:%d:%d\n",
                   dev.vendor_id, dev.device_id, dev.bus, dev.slot, dev.func);
            
            /* Get BAR0 which contains the AHCI base address */
            u32 bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
            if (!bar0) {
                printk(T,"sata-ahci: Error reading BAR0\n");
                continue;
            }
            
            ahci_base = (volatile void*)(uptr)(bar0 & ~0xF);
            sata_controller_count++;
            found++;
        }
    }
    
    if (found == 0) {
        printk(T,"No SATA controllers found\n");
    } else {
        printk(T,"Found %d SATA controller(s)\n", found);
        printk(T,"SATA subsystem initialized\n");
    }
}

u32 sata_device_count(void) {
    if (!ahci_base) {
        return 0;
    }
    
    volatile u32 *pi = (volatile u32*)((uptr)ahci_base + AHCI_PI);
    u32 ports = *pi;
    
    u32 count = 0;
    for (int i = 0; i < 32; i++) {
        if (ports & (1 << i)) {
            count++;
        }
    }
    
    return count;
}

int sata_get_device(u32 index, sata_device_t *dev) {
    if (!dev || !ahci_base || index >= 32) {
        return -1;
    }
    
    volatile u32 *pi = (volatile u32*)((uptr)ahci_base + AHCI_PI);
    u32 ports = *pi;
    
    u32 count = 0;
    for (int i = 0; i < 32; i++) {
        if (ports & (1 << i)) {
            if (count == index) {
                dev->port = i;
                dev->bus = 0;
                dev->capacity = 0; /* unknown until we query */
                /* later code may fill capacity; show if present */
                if (dev->capacity) {
                    char hsz[32];
                    fmt_size(dev->capacity * 512U, hsz, sizeof(hsz));
                    printk(T,"sata: device %u capacity %u sectors (%s)\n",
                           index, dev->capacity, hsz);
                }
                return 0;
            }
            count++;
        }
    }
    
    return -1;
}

int sata_read(u8 bus, u8 port, u32 lba, u32 count, void *buffer) {
    (void)bus;  /* Unused in stub */
    (void)port;  /* Unused in stub */
    (void)lba;  /* Unused in stub */
    (void)count;  /* Unused in stub */
    (void)buffer;  /* Unused in stub */
    
    if (!buffer || count == 0) {
        return -1;
    }
    
    printk(T,"sata-ahci: Read operation not yet implemented\n");
    
    return -1;
}

int sata_write(u8 bus, u8 port, u32 lba, u32 count, const void *buffer) {
    (void)bus;  /* Unused in stub */
    (void)port;  /* Unused in stub */
    (void)lba;  /* Unused in stub */
    (void)count;  /* Unused in stub */
    (void)buffer;  /* Unused in stub */
    
    if (!ahci_base || !buffer || count == 0) {
        return -1;
    }
    
    printk(T,"sata-ahci: Write operation not yet implemented\n");
    
    return -1;
}
