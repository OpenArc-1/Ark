/*
 * usb/usb_msd.c — USB Mass Storage Device (MSD) class driver
 *
 * Handles USB flash drives, external hard disks, SD card readers, etc.
 * bInterfaceClass=0x08, bInterfaceSubClass=0x06 (SCSI), bInterfaceProtocol=0x50 (BOT)
 *
 * Transport: Bulk-Only Transport (BOT)
 *   CBW  Command Block Wrapper (31 bytes OUT) — wraps a SCSI CDB
 *   Data phase (IN or OUT, length from CBW)
 *   CSW  Command Status Wrapper (13 bytes IN) — result code
 *
 * SCSI commands used:
 *   INQUIRY         0x12  — device identification string
 *   TEST_UNIT_READY 0x00  — check if media present
 *   READ_CAPACITY   0x25  — get block count and block size
 *   READ(10)        0x28  — read blocks (LBA, count)
 *   WRITE(10)       0x2A  — write blocks (LBA, count)
 *   REQUEST_SENSE   0x03  — get error detail after CHECK CONDITION
 *
 * CBW structure (31 bytes):
 *   dCBWSignature  0x43425355 ('USBC')
 *   dCBWTag        host-chosen 32-bit tag, echoed back in CSW
 *   dCBWDataTransferLength  bytes to transfer in data phase
 *   bmCBWFlags     0x80=IN (device→host), 0x00=OUT
 *   bCBWLUN        logical unit number (0 for most devices)
 *   bCBWCBLength   length of CBWCB (1-16)
 *   CBWCB[16]      SCSI command descriptor block
 *
 * CSW structure (13 bytes):
 *   dCSWSignature  0x53425355 ('USBS')
 *   dCSWTag        echoed from CBW
 *   dCSWDataResidue  bytes not transferred
 *   bCSWStatus     0=passed, 1=failed, 2=phase error
 *
 * Ark integration:
 *   usb_msd_init(dev)           — enumerate, get capacity
 *   usb_msd_read(lba, buf, n)   — read n 512-byte sectors from LBA
 *   usb_msd_write(lba, buf, n)  — write n 512-byte sectors to LBA
 *   usb_msd_get_capacity()      — returns total sector count
 */
#include "ark/types.h"
#include "ark/printk.h"

/* CBW / CSW signatures */
#define MSD_CBW_SIG  0x43425355u   /* 'USBC' little-endian */
#define MSD_CSW_SIG  0x53425355u   /* 'USBS' little-endian */

/* SCSI commands */
#define SCSI_INQUIRY          0x12
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_READ_CAPACITY    0x25
#define SCSI_READ10           0x28
#define SCSI_WRITE10          0x2A
#define SCSI_REQUEST_SENSE    0x03

typedef struct __attribute__((packed)) {
    u32 signature;
    u32 tag;
    u32 data_len;
    u8  flags;
    u8  lun;
    u8  cb_len;
    u8  cb[16];
} usb_msd_cbw_t;

typedef struct __attribute__((packed)) {
    u32 signature;
    u32 tag;
    u32 residue;
    u8  status;
} usb_msd_csw_t;

static u32 msd_sector_count = 0;
static u32 msd_sector_size  = 512;
static bool msd_ready = false;

static void usb_msd_build_cbw(usb_msd_cbw_t *cbw, u32 tag,
                               u32 datalen, bool in,
                               const u8 *cdb, u8 cdblen) {
    cbw->signature = MSD_CBW_SIG;
    cbw->tag       = tag;
    cbw->data_len  = datalen;
    cbw->flags     = in ? 0x80u : 0x00u;
    cbw->lun       = 0;
    cbw->cb_len    = cdblen;
    for (int i = 0; i < 16; i++) cbw->cb[i] = (i < cdblen) ? cdb[i] : 0;
}

bool usb_msd_init(u8 addr, u8 ep_in, u8 ep_out) {
    /* In a real implementation:
     * 1. Send GET_MAX_LUN (class request) to find number of LUNs
     * 2. Send INQUIRY CBW, read 36-byte response
     * 3. Send TEST_UNIT_READY CBW, check CSW status
     * 4. Send READ_CAPACITY CBW, read 8-byte response
     *    bytes 0-3 = last LBA (big-endian)
     *    bytes 4-7 = block size (big-endian)
     * 5. Compute total_sectors = last_lba + 1
     */
    (void)addr; (void)ep_in; (void)ep_out;
    printk(T, "[MSD] addr=%u ep_in=%u ep_out=%u — init (stub)\n",
           addr, ep_in, ep_out);
    msd_ready = false;
    return false; /* stub — no hardware transaction yet */
}

bool usb_msd_read(u32 lba, u8 *buf, u32 sector_count) {
    if (!msd_ready) return false;
    /* Build READ(10) CDB: 0x28 0x00 LBA[4] 0x00 count[2] 0x00 */
    u8 cdb[10] = {
        SCSI_READ10, 0,
        (u8)(lba>>24), (u8)(lba>>16), (u8)(lba>>8), (u8)lba,
        0,
        (u8)(sector_count>>8), (u8)sector_count,
        0
    };
    (void)buf; (void)cdb;
    return false; /* stub */
}

bool usb_msd_write(u32 lba, const u8 *buf, u32 sector_count) {
    if (!msd_ready) return false;
    u8 cdb[10] = {
        SCSI_WRITE10, 0,
        (u8)(lba>>24), (u8)(lba>>16), (u8)(lba>>8), (u8)lba,
        0,
        (u8)(sector_count>>8), (u8)sector_count,
        0
    };
    (void)buf; (void)cdb;
    return false; /* stub */
}

u32  usb_msd_get_sector_count(void) { return msd_sector_count; }
u32  usb_msd_get_sector_size(void)  { return msd_sector_size;  }
bool usb_msd_ready(void)            { return msd_ready;        }
