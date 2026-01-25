/**
 * ATA (Advanced Technology Attachment) Driver
 *
 * Implements IDE hard drive access for legacy systems.
 */

#include "ata.h"
#include "ark/printk.h"
#include "../io/built-in.h"

/* ATA Command Register Offsets */
#define ATA_DATA        0x00
#define ATA_ERROR       0x01
#define ATA_FEATURES    0x01
#define ATA_SECTOR_COUNT 0x02
#define ATA_SECTOR_NUM  0x03
#define ATA_CYLINDER_LOW 0x04
#define ATA_CYLINDER_HIGH 0x05
#define ATA_DRIVE_HEAD  0x06
#define ATA_STATUS      0x07
#define ATA_COMMAND     0x07

/* ATA Commands */
#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_IDENT   0xEC

/* ATA Status Bits */
#define ATA_STATUS_BUSY 0x80
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

/* Primary IDE ports */
#define ATA_PRIMARY_CMD   0x1F0
#define ATA_PRIMARY_CTRL  0x3F6

/* Secondary IDE ports */
#define ATA_SECONDARY_CMD   0x170
#define ATA_SECONDARY_CTRL  0x376

static u32 ata_device_count = 0;
static ata_device_t ata_devices[4];

static u16 ata_primary_cmd = ATA_PRIMARY_CMD;
/* static u16 ata_primary_ctrl = ATA_PRIMARY_CTRL; */
static u16 ata_secondary_cmd = ATA_SECONDARY_CMD;
/* static u16 ata_secondary_ctrl = ATA_SECONDARY_CTRL; */

static u8 ata_wait_drq(u16 port) {
    for (int i = 0; i < 30000; i++) {
        u8 status = inb(port + ATA_STATUS);
        if (status & ATA_STATUS_DRQ) {
            return status;
        }
        if (status & ATA_STATUS_ERR) {
            return 0xFF;
        }
    }
    return 0xFF;
}

static void ata_identify(u8 bus, u8 drive) {
    u16 base_port = (bus == 0) ? ata_primary_cmd : ata_secondary_cmd;
    
    /* Select drive */
    u8 head = (drive == 0) ? 0xA0 : 0xB0;
    outb(base_port + ATA_DRIVE_HEAD, head);
    
    /* Set sector count and sector number */
    outb(base_port + ATA_SECTOR_COUNT, 0);
    outb(base_port + ATA_SECTOR_NUM, 0);
    outb(base_port + ATA_CYLINDER_LOW, 0);
    outb(base_port + ATA_CYLINDER_HIGH, 0);
    
    /* Issue IDENTIFY command */
    outb(base_port + ATA_COMMAND, ATA_CMD_IDENT);
    
    /* Wait for device to respond */
    u8 status = ata_wait_drq(base_port);
    if (status == 0xFF || (status & ATA_STATUS_ERR)) {
        return;  /* Device not present or error */
    }
    
    /* Read identify data */
    u16 buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(base_port + ATA_DATA);
    }
    
    /* Extract device info */
    ata_device_t *dev = &ata_devices[ata_device_count];
    dev->bus = bus;
    dev->drive = drive;
    dev->cylinders = buffer[1];
    dev->heads = buffer[3];
    dev->sectors = buffer[6];
    
    /* Copy LBA sectors count safely */
    dev->lba_sectors = buffer[60] | (buffer[61] << 16);
    dev->type = ATA_TYPE_ATA;
    
    ata_device_count++;
    printk("[    0.000162] ata: Found device at bus %d drive %d: %u sectors\n", 
           bus, drive, dev->lba_sectors);
}

void ata_init(void) {
    printk("[    0.000161] Probing ATA controllers (IDE)...\n");
    
    ata_device_count = 0;
    
    /* Probe primary bus, both drives */
    ata_identify(0, 0);
    ata_identify(0, 1);
    
    /* Probe secondary bus, both drives */
    ata_identify(1, 0);
    ata_identify(1, 1);
    
    if (ata_device_count == 0) {
        printk("[    0.000162] No ATA devices found\n");
    } else {
        printk("[    0.000162] Found %d ATA device(s)\n", ata_device_count);
    }
}

int ata_read(u8 bus, u8 drive, u32 lba, u32 count, void *buffer) {
    if (!buffer || count == 0 || count > 256) {
        return -1;
    }
    
    if (bus > 1 || drive > 1) {
        return -1;
    }
    
    u16 base_port = (bus == 0) ? ata_primary_cmd : ata_secondary_cmd;
    
    /* Select drive and set LBA mode */
    u8 head = (drive == 0) ? 0xE0 : 0xF0;
    outb(base_port + ATA_DRIVE_HEAD, head | ((lba >> 24) & 0x0F));
    
    /* Set sector count, and LBA bytes */
    outb(base_port + ATA_SECTOR_COUNT, count);
    outb(base_port + ATA_SECTOR_NUM, lba & 0xFF);
    outb(base_port + ATA_CYLINDER_LOW, (lba >> 8) & 0xFF);
    outb(base_port + ATA_CYLINDER_HIGH, (lba >> 16) & 0xFF);
    
    /* Issue READ command */
    outb(base_port + ATA_COMMAND, ATA_CMD_READ);
    
    /* Read sectors */
    u16 *buf = (u16*)buffer;
    for (u32 s = 0; s < count; s++) {
        u8 status = ata_wait_drq(base_port);
        if (status == 0xFF || (status & ATA_STATUS_ERR)) {
            return -1;
        }
        
        for (int i = 0; i < 256; i++) {
            *buf++ = inw(base_port + ATA_DATA);
        }
    }
    
    return 0;
}

int ata_write(u8 bus, u8 drive, u32 lba, u32 count, const void *buffer) {
    (void)bus;  /* Unused */
    (void)drive;  /* Unused */
    (void)lba;  /* Unused */
    (void)count;  /* Unused */
    (void)buffer;  /* Unused */
    
    if (!buffer || count == 0 || count > 256) {
        return -1;
    }
    
    if (bus > 1 || drive > 1) {
        return -1;
    }
    
    printk("    ata: Write operation not yet implemented\n");
    
    return -1;
}
