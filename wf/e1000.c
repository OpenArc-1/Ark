// e1000.c
#include "ark/types.h"
#include "ark/mmio.h"
#include "ark/pci.h"
#include "ark/mem.h"
#include "ark/printk.h"
#include "ark/net.h"  /* register network driver */

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 8
#define PACKET_SIZE   2048

#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_RCTRL     0x0100
#define E1000_TCTRL     0x0400

#define E1000_RXDESCLO   0x2800
#define E1000_RXDESCHI   0x2804
#define E1000_RXDESCLEN  0x2808
#define E1000_RXDESCHEAD 0x2810
#define E1000_RXDESCTAIL 0x2818

#define E1000_TXDESCLO   0x3800
#define E1000_TXDESCHI   0x3804
#define E1000_TXDESCLEN  0x3808
#define E1000_TXDESCHEAD 0x3810
#define E1000_TXDESCTAIL 0x3818

#define E1000_CTRL_RST  (1<<26)

struct rx_desc {
    u64 addr;
    u16 length;
    u16 checksum;
    u8  status;
    u8  errors;
    u16 special;
} __attribute__((packed));

struct tx_desc {
    u64 addr;
    u16 length;
    u8  cso;
    u8  cmd;
    u8  status;
    u8  css;
    u16 special;
} __attribute__((packed));

static volatile u8 *e1000_mmio;

/* forward declarations */
static void e1000_init(void);

/* ------------------------------------------------------------------ */
/* Helper wrappers for net_driver interface                        */
/* ------------------------------------------------------------------ */
static int e1000_probe(void) {
    pci_device_t dev;
    for (int bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            if (!pci_get_device((u8)bus, slot, 0, &dev))
                continue;
            if (dev.vendor_id == 0x8086 && dev.device_id == 0x100E)
                return 0;
        }
    }
    return -1;
}

static int e1000_get_mac(u8 mac[6]) {
    if (!e1000_mmio) return -1;
    /* MAC is in RAL/RAH registers at 0x5400 & 0x5404 */
    u32 ral = mmio_read32(e1000_mmio, 0x5400);
    u32 rah = mmio_read32(e1000_mmio, 0x5404);
    mac[0] = ral & 0xFF;
    mac[1] = (ral >> 8) & 0xFF;
    mac[2] = (ral >> 16) & 0xFF;
    mac[3] = (ral >> 24) & 0xFF;
    mac[4] = rah & 0xFF;
    mac[5] = (rah >> 8) & 0xFF;
    return 0;
}

static int e1000_send_wrapper(const void *buf, u32 len) {
    e1000_send((void*)buf, (u16)len);
    return 0;
}

static int e1000_recv_wrapper(void *buf, u32 maxlen) {
    return e1000_recv(buf);
}

/* driver descriptor */
static net_driver_t e1000_driver = {
    .name = "e1000",
    .probe = e1000_probe,
    .init  = e1000_init,
    .send  = e1000_send_wrapper,
    .recv  = e1000_recv_wrapper,
    .get_mac = e1000_get_mac,
    .next = NULL,
};

/* register during static init */
static void __attribute__((constructor)) register_e1000_driver(void) {
    net_register_driver(&e1000_driver);
}

static struct rx_desc rx_ring[RX_DESC_COUNT] __attribute__((aligned(16)));
static struct tx_desc tx_ring[TX_DESC_COUNT] __attribute__((aligned(16)));

static u8 rx_bufs[RX_DESC_COUNT][PACKET_SIZE];
static u8 tx_bufs[TX_DESC_COUNT][PACKET_SIZE];

static u32 rx_cur = 0;
static u32 tx_cur = 0;

// ---------------- RX INIT ----------------
static void e1000_rx_init() {
    for (int i = 0; i < RX_DESC_COUNT; i++) {
        rx_ring[i].addr   = (u64)(u32)rx_bufs[i];
        rx_ring[i].status = 0;
    }

    u32 rx_phys = (u32)rx_ring;
    mmio_write32(e1000_mmio, E1000_RXDESCLO,   rx_phys);
    mmio_write32(e1000_mmio, E1000_RXDESCHI,   0);
    mmio_write32(e1000_mmio, E1000_RXDESCLEN,  RX_DESC_COUNT * sizeof(struct rx_desc));
    mmio_write32(e1000_mmio, E1000_RXDESCHEAD, 0);
    mmio_write32(e1000_mmio, E1000_RXDESCTAIL, RX_DESC_COUNT - 1);

    mmio_write32(e1000_mmio, E1000_RCTRL,
        (1<<1)  |   // Receiver enable
        (1<<15) |   // Broadcast accept
        (1<<26) |   // Buffer size 2048
        (1<<4));    // Strip CRC
}

// ---------------- TX INIT ----------------
static void e1000_tx_init() {
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        tx_ring[i].addr   = (u64)(u32)tx_bufs[i];
        tx_ring[i].status = 1;
    }

    u32 tx_phys = (u32)tx_ring;
    mmio_write32(e1000_mmio, E1000_TXDESCLO,   tx_phys);
    mmio_write32(e1000_mmio, E1000_TXDESCHI,   0);
    mmio_write32(e1000_mmio, E1000_TXDESCLEN,  TX_DESC_COUNT * sizeof(struct tx_desc));
    mmio_write32(e1000_mmio, E1000_TXDESCHEAD, 0);
    mmio_write32(e1000_mmio, E1000_TXDESCTAIL, 0);

    mmio_write32(e1000_mmio, E1000_TCTRL,
        (1<<1) |    // Transmit enable
        (1<<3));    // Pad short packets
}

// ---------------- SEND ----------------
void e1000_send(void *data, u16 len) {
    if (len > PACKET_SIZE) {
        printk("E1000: packet too large (%d)\n", len);
        return;
    }

    u32 i = tx_cur;
    while (!(tx_ring[i].status & 1));

    memcpy(tx_bufs[i], data, len);
    tx_ring[i].length = len;
    tx_ring[i].cmd    = (1<<0) | (1<<1) | (1<<3); // EOP | IFCS | RS
    tx_ring[i].status = 0;

    tx_cur = (i + 1) % TX_DESC_COUNT;
    mmio_write32(e1000_mmio, E1000_TXDESCTAIL, tx_cur);
}

// ---------------- RECEIVE ----------------
int e1000_recv(void *out) {
    u32 i = rx_cur;

    if (!(rx_ring[i].status & 1))
        return 0;

    u16 len = rx_ring[i].length;
    if (len > PACKET_SIZE)
        len = PACKET_SIZE;

    memcpy(out, rx_bufs[i], len);

    rx_ring[i].status = 0;
    mmio_write32(e1000_mmio, E1000_RXDESCTAIL, i);
    rx_cur = (i + 1) % RX_DESC_COUNT;

    return (int)len;
}

// ---------------- INIT ----------------
void e1000_init() {
    printk("E1000: scanning PCI...\n");

    pci_device_t dev;
    int found = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            // Use your kernel's own pci_get_device instead of raw pciread
            if (!pci_get_device((u8)bus, slot, 0, &dev))
                continue;

            if (dev.vendor_id == 0x8086 && dev.device_id == 0x100E) {
                printk("E1000 found at %d:%d\n", bus, slot);
                found = 1;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        printk("E1000 not found\n");
        return;
    }

    // --- CRITICAL: Enable Memory Space (bit1) + Bus Master (bit2) ---
    // Without this the BAR returns 0 or garbage and MMIO access faults
    u32 cmd = pciread(dev.bus, dev.slot, 0, 0x04);
    cmd |= (1<<1) | (1<<2);
    pciwrite(dev.bus, dev.slot, 0, 0x04, cmd);

    // --- Read and validate BAR0 ---
    u32 bar0 = pci_read_bar(dev.bus, dev.slot, 0, 0);
    printk("E1000: BAR0 raw = 0x%x\n", bar0);

    if (bar0 & 0x1) {
        printk("E1000: BAR0 is IO space, expected MMIO — aborting\n");
        return;
    }

    u32 mmio_base = bar0 & ~0xFu;
    if (mmio_base == 0 || mmio_base == 0xFFFFFFF0u) {
        printk("E1000: BAR0 invalid (0x%x) — aborting\n", mmio_base);
        return;
    }

    printk("E1000: MMIO base = 0x%x\n", mmio_base);
    e1000_mmio = (volatile u8*)mmio_map(mmio_base, 0x20000);

    // --- Reset ---
    mmio_write32(e1000_mmio, E1000_CTRL,
        mmio_read32(e1000_mmio, E1000_CTRL) | E1000_CTRL_RST);

    u32 timeout = 100000;
    while ((mmio_read32(e1000_mmio, E1000_CTRL) & E1000_CTRL_RST) && --timeout);
    if (!timeout) {
        printk("E1000: reset timed out\n");
        return;
    }

    printk("E1000: reset OK\n");

    e1000_rx_init();
    e1000_tx_init();

    printk("E1000 ready\n");
}