// e1000.c
#include <stdint.h>
#include "ark/mmio.h"
#include "ark/pci.h"
#include "ark/mem.h"
#include "ark/printk.h"

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
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

static volatile uint8_t *e1000_mmio;

static struct rx_desc rx_ring[RX_DESC_COUNT] __attribute__((aligned(16)));
static struct tx_desc tx_ring[TX_DESC_COUNT] __attribute__((aligned(16)));

static uint8_t rx_bufs[RX_DESC_COUNT][PACKET_SIZE];
static uint8_t tx_bufs[TX_DESC_COUNT][PACKET_SIZE];

static uint32_t rx_cur = 0;
static uint32_t tx_cur = 0;

// ---------------- RX INIT ----------------
static void e1000_rx_init() {
    for (int i = 0; i < RX_DESC_COUNT; i++) {
        rx_ring[i].addr   = (uint64_t)(uint32_t)rx_bufs[i];
        rx_ring[i].status = 0;
    }

    uint32_t rx_phys = (uint32_t)rx_ring;
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
        tx_ring[i].addr   = (uint64_t)(uint32_t)tx_bufs[i];
        tx_ring[i].status = 1;
    }

    uint32_t tx_phys = (uint32_t)tx_ring;
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
void e1000_send(void *data, uint16_t len) {
    if (len > PACKET_SIZE) {
        printk("E1000: packet too large (%d)\n", len);
        return;
    }

    uint32_t i = tx_cur;
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
    uint32_t i = rx_cur;

    if (!(rx_ring[i].status & 1))
        return 0;

    uint16_t len = rx_ring[i].length;
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

    uint32_t mmio_base = bar0 & ~0xFu;
    if (mmio_base == 0 || mmio_base == 0xFFFFFFF0u) {
        printk("E1000: BAR0 invalid (0x%x) — aborting\n", mmio_base);
        return;
    }

    printk("E1000: MMIO base = 0x%x\n", mmio_base);
    e1000_mmio = (volatile uint8_t*)mmio_map(mmio_base, 0x20000);

    // --- Reset ---
    mmio_write32(e1000_mmio, E1000_CTRL,
        mmio_read32(e1000_mmio, E1000_CTRL) | E1000_CTRL_RST);

    uint32_t timeout = 100000;
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