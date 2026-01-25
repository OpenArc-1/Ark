// e1000.c
#include <stdint.h>
#include "../mem/mmio.h"
#include "../gen/pci.h"
#include "../mem/mem.h"
#include "ark/printk.h"

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 8
#define PACKET_SIZE   2048

// Registers
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

// RX Descriptor
struct rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

// TX Descriptor
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
        rx_ring[i].addr = (uint64_t)&rx_bufs[i];
        rx_ring[i].status = 0;
    }

    mmio_write32(e1000_mmio, E1000_RXDESCLO, (uint32_t)(uint64_t)rx_ring);
    mmio_write32(e1000_mmio, E1000_RXDESCHI, 0);
    mmio_write32(e1000_mmio, E1000_RXDESCLEN, RX_DESC_COUNT * sizeof(struct rx_desc));
    mmio_write32(e1000_mmio, E1000_RXDESCHEAD, 0);
    mmio_write32(e1000_mmio, E1000_RXDESCTAIL, RX_DESC_COUNT - 1);

    mmio_write32(e1000_mmio, E1000_RCTRL,
        (1<<1)  |  // Receiver enable
        (1<<15) |  // Broadcast accept
        (1<<26) |  // Buffer size 2048
        (1<<4));   // Strip CRC
}

// ---------------- TX INIT ----------------
static void e1000_tx_init() {
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        tx_ring[i].addr = (uint64_t)&tx_bufs[i];
        tx_ring[i].status = 1;
    }

    mmio_write32(e1000_mmio, E1000_TXDESCLO, (uint32_t)(uint64_t)tx_ring);
    mmio_write32(e1000_mmio, E1000_TXDESCHI, 0);
    mmio_write32(e1000_mmio, E1000_TXDESCLEN, TX_DESC_COUNT * sizeof(struct tx_desc));
    mmio_write32(e1000_mmio, E1000_TXDESCHEAD, 0);
    mmio_write32(e1000_mmio, E1000_TXDESCTAIL, 0);

    mmio_write32(e1000_mmio, E1000_TCTRL,
        (1<<1) |  // Transmit enable
        (1<<3));  // Pad short packets
}

// ---------------- SEND ----------------
void e1000_send(void *data, uint16_t len) {
    uint32_t i = tx_cur;

    while (!(tx_ring[i].status & 1));

    memcpy(tx_bufs[i], data, len);

    tx_ring[i].length = len;
    tx_ring[i].cmd = (1<<0) | (1<<3) | (1<<1); // EOP | RS | IFCS
    tx_ring[i].status = 0;

    tx_cur = (i + 1) % TX_DESC_COUNT;
    mmio_write32(e1000_mmio, E1000_TXDESCTAIL, tx_cur);
}

// ---------------- RECEIVE ----------------
int e1000_recv(void *out) {
    uint32_t i = rx_cur;

    if (!(rx_ring[i].status & 1))
        return 0;

    int len = rx_ring[i].length;
    memcpy(out, rx_bufs[i], len);

    rx_ring[i].status = 0;
    rx_cur = (i + 1) % RX_DESC_COUNT;
    mmio_write32(e1000_mmio, E1000_RXDESCTAIL, rx_cur);

    return len;
}

// ---------------- INIT ----------------
void e1000_init() {
    printk("E1000: scanning PCI...\n");

    for (uint8_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pciread(bus, slot, 0, 0x00);
            uint16_t vendor = vendor_device & 0xFFFF;
            if (vendor != 0x8086) continue;

            uint16_t device = (vendor_device >> 16) & 0xFFFF;
            if (device == 0x100E) {
                printk("E1000 found at %d:%d\n", bus, slot);

                uint32_t bar0 = pci_read_bar(bus, slot, 0, 0);
                e1000_mmio = (volatile uint8_t*)(uintptr_t)(bar0 & ~0xF);

                goto found;
            }
        }
    }

    printk("E1000 not found\n");
    return;

found:
    printk("E1000 MMIO mapped\n");

    mmio_write32(e1000_mmio, E1000_CTRL,
        mmio_read32(e1000_mmio, E1000_CTRL) | (1<<26));

    e1000_rx_init();
    e1000_tx_init();

    printk("E1000 ready\n");
}

