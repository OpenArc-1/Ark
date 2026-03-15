/*
 * rtl8139.c — Full Realtek RTL8139 NIC driver for Ark kernel
 *
 * Covers PCI vendor 0x10EC / device 0x8139.
 * Uses I/O-port access (the RTL8139 exposes registers as I/O space BAR0).
 *
 * Ring layout
 *   RX: single 8 KB + header wrap-around ring (RTL "Ring Buffer" mode)
 *   TX: 4 fixed 2 KB descriptor slots, round-robin
 */

#include "ark/net.h"
#include "ark/pci.h"
#include "ark/printk.h"
#include "ark/mem.h"
#include "ark/types.h"

/* ── I/O-port register offsets ─────────────────────────────────────── */
#define RTL_IDR0        0x00    /* MAC address bytes 0-5               */
#define RTL_MAR0        0x08    /* Multicast filter                    */
#define RTL_TSD0        0x10    /* TX status descriptors 0-3 (4xu32)  */
#define RTL_TSAD0       0x20    /* TX start address descriptors 0-3   */
#define RTL_RBSTART     0x30    /* RX buffer start address             */
#define RTL_CR          0x37    /* Command Register                    */
#define RTL_CAPR        0x38    /* Current Address of Packet Read      */
#define RTL_CBR         0x3A    /* Current Buffer Address              */
#define RTL_IMR         0x3C    /* Interrupt Mask Register             */
#define RTL_ISR         0x3E    /* Interrupt Status Register           */
#define RTL_TCR         0x40    /* TX Configuration Register           */
#define RTL_RCR         0x44    /* RX Configuration Register           */
#define RTL_9346CR      0x50    /* 93C46 Command Register              */
#define RTL_CONFIG1     0x52

/* Command register bits */
#define CR_RST          0x10
#define CR_RE           0x08
#define CR_TE           0x04
#define CR_BUFE         0x01

/* RX header flags */
#define RX_ROK          0x0001

/* RCR bits */
#define RCR_APM         (1<<1)
#define RCR_AM          (1<<2)
#define RCR_AB          (1<<3)
#define RCR_WRAP        (1<<7)
#define RCR_MXDMA_UNLIM (7<<8)
#define RCR_RBLEN_8K    (0<<11)
#define RCR_RXFTH_NONE  (7<<13)

/* TCR bits */
#define TCR_MXDMA_2048  (7<<8)
#define TCR_IFG_STD     (3<<24)

#define RX_BUF_SIZE     (8192 + 16 + 1500)
#define TX_BUF_SIZE     2048
#define TX_DESC_COUNT   4

/* ── port I/O helpers ───────────────────────────────────────────────── */
static inline void _outb(u16 p, u8  v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void _outw(u16 p, u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void _outl(u16 p, u32 v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline u8  _inb(u16 p){u8  v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u16 _inw(u16 p){u16 v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u32 _inl(u16 p){u32 v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}

static u16 rtl_iobase = 0;
static u8  rtl_rx_buf[RX_BUF_SIZE] __attribute__((aligned(4)));
static u8  rtl_tx_buf[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(4)));
static u32 rtl_rx_offset = 0;
static u32 rtl_tx_cur = 0;

#define R8(r)    _inb ((u16)(rtl_iobase+(r)))
#define R16(r)   _inw ((u16)(rtl_iobase+(r)))
#define R32(r)   _inl ((u16)(rtl_iobase+(r)))
#define W8(r,v)  _outb((u16)(rtl_iobase+(r)),(u8)(v))
#define W16(r,v) _outw((u16)(rtl_iobase+(r)),(u16)(v))
#define W32(r,v) _outl((u16)(rtl_iobase+(r)),(u32)(v))

static int rtl8139_probe(void) {
    pci_device_t dev;
    for_each_pci_device(dev)
        if (dev.vendor_id == 0x10EC && dev.device_id == 0x8139) return 0;
    return -1;
}

static int rtl8139_init(void) {
    pci_device_t dev;
    int found = 0;
    for_each_pci_device(dev) {
        if (dev.vendor_id == 0x10EC && dev.device_id == 0x8139) { found=1; break; }
    }
    if (!found) return -1;

    /* Enable I/O space + bus mastering */
    u32 cmd = pciread(dev.bus, dev.slot, dev.func, 0x04);
    pciwrite(dev.bus, dev.slot, dev.func, 0x04, cmd | (1<<0)|(1<<2));

    u32 bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
    rtl_iobase = (u16)(bar0 & 0xFFFC);
    printk(T,"rtl8139: I/O base=0x%x\n",(u32)rtl_iobase);

    /* Power on + reset */
    W8(RTL_CONFIG1, 0x00);
    W8(RTL_9346CR, 0xC0);  /* unlock */
    W8(RTL_CR, CR_RST);
    volatile u32 t = 100000;
    while ((R8(RTL_CR) & CR_RST) && --t);
    if (!t) { printk(T,"rtl8139: reset timeout\n"); return -1; }

    /* RX ring */
    W32(RTL_RBSTART, (u32)rtl_rx_buf);
    rtl_rx_offset = 0;

    /* TX descriptors */
    for (int i = 0; i < TX_DESC_COUNT; i++)
        W32(RTL_TSAD0 + i*4, (u32)rtl_tx_buf[i]);

    W32(RTL_RCR, RCR_AB|RCR_APM|RCR_AM|RCR_WRAP|RCR_MXDMA_UNLIM|RCR_RBLEN_8K|RCR_RXFTH_NONE);
    W32(RTL_TCR, TCR_MXDMA_2048|TCR_IFG_STD);
    W16(RTL_IMR, 0x0000);
    W16(RTL_ISR, 0xFFFF);
    W8(RTL_9346CR, 0x00);  /* lock */
    W8(RTL_CR, CR_RE|CR_TE);

    printk(T,"rtl8139: ready\n");
    return 0;
}

static int rtl8139_send(const void *buf, u32 len) {
    if (!rtl_iobase || len > TX_BUF_SIZE) return -1;
    u32 slot = rtl_tx_cur;
    /* wait for NIC to release descriptor (OWN=1 means NIC owns) */
    volatile u32 t = 200000;
    while (!(R32(RTL_TSD0 + slot*4) & (1<<13)) && --t);
    memcpy(rtl_tx_buf[slot], buf, len);
    if (len < 60) { memset(rtl_tx_buf[slot]+len, 0, 60-len); len=60; }
    W32(RTL_TSD0 + slot*4, len & 0x1FFF);
    rtl_tx_cur = (slot+1) % TX_DESC_COUNT;
    return 0;
}

static int rtl8139_recv(void *buf, u32 maxlen) {
    if (!rtl_iobase || (R8(RTL_CR) & CR_BUFE)) return 0;
    u8 *ring = rtl_rx_buf;
    u16 status = *(u16*)(ring + rtl_rx_offset);
    u16 plen   = *(u16*)(ring + rtl_rx_offset + 2);
    if (!(status & RX_ROK)) {
        W16(RTL_CAPR, R16(RTL_CBR) - 16);
        rtl_rx_offset = 0;
        return 0;
    }
    u16 dlen = plen - 4;
    if (dlen > maxlen) dlen = (u16)maxlen;
    u32 off = rtl_rx_offset + 4;
    u32 rs  = 8192;
    if (off + dlen <= rs) {
        memcpy(buf, ring + off, dlen);
    } else {
        u32 first = rs - off;
        memcpy(buf,                ring + off, first);
        memcpy((u8*)buf + first,   ring,       dlen - first);
    }
    rtl_rx_offset = ((rtl_rx_offset + plen + 4 + 3) & ~3) % rs;
    W16(RTL_CAPR, (u16)(rtl_rx_offset - 16));
    return (int)dlen;
}

static int rtl8139_get_mac(u8 mac[6]) {
    if (!rtl_iobase) return -1;
    for (int i = 0; i < 6; i++) mac[i] = R8(RTL_IDR0 + i);
    return 0;
}

static net_driver_t rtl8139_driver = {
    .name="rtl8139", .probe=rtl8139_probe, .init=rtl8139_init,
    .send=rtl8139_send, .recv=rtl8139_recv, .get_mac=rtl8139_get_mac,
    .next=NULL,
};

void rtl8139_register(void) { net_register_driver(&rtl8139_driver); }
net_driver_t *rtl8139_get_driver(void) { return &rtl8139_driver; }
