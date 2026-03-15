/*
 * pcnet.c — AMD PCnet-PCI II (Am79C970A) network driver for Ark kernel
 *
 * PCI vendor 0x1022, device 0x2000.
 * Used by QEMU's default "pcnet" NIC and VMware 5.x guests.
 *
 * Uses 32-bit "DWIO" register access via I/O ports:
 *   RAP  (Register Address Port) at base+0x14
 *   RDP  (Register Data Port)    at base+0x10
 *   RESET at base+0x18
 *   BDP  (Bus Data Port)         at base+0x1C (BCSR)
 */

#include "ark/net.h"
#include "ark/pci.h"
#include "ark/printk.h"
#include "ark/mem.h"
#include "ark/types.h"

#define PCNET_RESET  0x18
#define PCNET_RAP32  0x14
#define PCNET_RDP32  0x10
#define PCNET_BDP32  0x1C

/* CSR registers */
#define CSR0_INIT   (1<<0)
#define CSR0_STRT   (1<<1)
#define CSR0_STOP   (1<<2)
#define CSR0_TX     (1<<3)
#define CSR0_RXON   (1<<6)
#define CSR0_IENA   (1<<6)

#define RX_RING  16
#define TX_RING  8
#define PKT_SZ   2048

/* PCnet Initialization Block (32-bit mode) */
struct pcnet_initblock {
    u16 mode;
    u8  rlen;     /* log2(rx ring size) in upper nibble */
    u8  tlen;     /* log2(tx ring size) in upper nibble */
    u8  padr[6];  /* MAC */
    u16 _res;
    u32 ladrf[2]; /* logical address filter */
    u32 rdra;     /* RX descriptor ring address */
    u32 tdra;     /* TX descriptor ring address */
} __attribute__((packed));

/* 32-bit PCnet descriptor */
struct pcnet_rx_desc {
    u32 buf_addr;
    i16 buf_len;   /* negative 2's complement of buffer size */
    u16 status;    /* OWN bit in bit15 of status */
    u32 msg_len;
    u32 _res;
} __attribute__((packed));

struct pcnet_tx_desc {
    u32 buf_addr;
    i16 buf_len;
    u16 status;
    u32 misc;
    u32 _res;
} __attribute__((packed));

#define DESC_OWN   (1<<15)   /* device owns descriptor */
#define DESC_STP   (1<<9)    /* start of packet        */
#define DESC_ENP   (1<<8)    /* end of packet          */

static u16 pc_iobase = 0;

static struct pcnet_initblock iblk __attribute__((aligned(4)));
static struct pcnet_rx_desc   rxd[RX_RING] __attribute__((aligned(16)));
static struct pcnet_tx_desc   txd[TX_RING] __attribute__((aligned(16)));
static u8 rx_buf[RX_RING][PKT_SZ] __attribute__((aligned(4)));
static u8 tx_buf[TX_RING][PKT_SZ] __attribute__((aligned(4)));

static u32 rx_cur = 0;
static u32 tx_cur = 0;

static inline void _po32(u16 p,u32 v){__asm__("outl %0,%1"::"a"(v),"Nd"(p));}
static inline u32  _pi32(u16 p){u32 v;__asm__("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u16  _pi16(u16 p){u16 v;__asm__("inw %1,%0":"=a"(v):"Nd"(p));return v;}

static void pcnet_write_csr(u32 csr, u32 val) {
    _po32((u16)(pc_iobase+PCNET_RAP32), csr);
    _po32((u16)(pc_iobase+PCNET_RDP32), val);
}
static u32 pcnet_read_csr(u32 csr) {
    _po32((u16)(pc_iobase+PCNET_RAP32), csr);
    return _pi32((u16)(pc_iobase+PCNET_RDP32));
}

static int pcnet_probe(void) {
    pci_device_t dev;
    for_each_pci_device(dev)
        if (dev.vendor_id == 0x1022 && dev.device_id == 0x2000) return 0;
    return -1;
}

static int pcnet_init(void) {
    pci_device_t dev;
    int found = 0;
    for_each_pci_device(dev) {
        if (dev.vendor_id == 0x1022 && dev.device_id == 0x2000) { found=1; break; }
    }
    if (!found) return -1;

    u32 cmd = pciread(dev.bus, dev.slot, dev.func, 0x04);
    pciwrite(dev.bus, dev.slot, dev.func, 0x04, cmd|(1<<0)|(1<<2));

    u32 bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
    pc_iobase = (u16)(bar0 & 0xFFFC);
    printk(T,"pcnet: I/O base=0x%x\n",(u32)pc_iobase);

    /* Reset (read RESET port, 16-bit) */
    (void)_pi16((u16)(pc_iobase + PCNET_RESET));
    volatile u32 d=100000; while(d--);

    /* Switch to 32-bit DWIO mode by writing to RDP32 once */
    _po32((u16)(pc_iobase+PCNET_RDP32), 0);

    /* Read MAC from APROM at I/O base+0x00 */
    u8 mac[6];
    for (int i = 0; i < 6; i++)
    {
        u8 b; __asm__("inb %1,%0":"=a"(b):"Nd"((u16)(pc_iobase+i))); mac[i]=b;
    }

    /* Init descriptors */
    for (int i = 0; i < RX_RING; i++) {
        rxd[i].buf_addr = (u32)rx_buf[i];
        rxd[i].buf_len  = (i16)(-(i16)PKT_SZ);
        rxd[i].status   = DESC_OWN;
        rxd[i].msg_len  = 0;
    }
    for (int i = 0; i < TX_RING; i++) {
        txd[i].buf_addr = (u32)tx_buf[i];
        txd[i].buf_len  = 0;
        txd[i].status   = 0;
    }

    /* Init block */
    memset(&iblk, 0, sizeof(iblk));
    iblk.mode = 0x0000;
    /* RLEN = log2(RX_RING)=4 in bits[7:4]; TLEN = log2(TX_RING)=3 in bits[7:4] */
    iblk.rlen = (4 << 4);
    iblk.tlen = (3 << 4);
    memcpy(iblk.padr, mac, 6);
    iblk.rdra = (u32)rxd;
    iblk.tdra = (u32)txd;

    /* Pass init block address via CSR1/CSR2 */
    u32 ib = (u32)&iblk;
    pcnet_write_csr(1, ib & 0xFFFF);
    pcnet_write_csr(2, ib >> 16);

    /* Set CSR4: auto TX pad */
    pcnet_write_csr(4, 0x0915);

    /* INIT */
    pcnet_write_csr(0, CSR0_INIT);
    d = 200000;
    while (!(pcnet_read_csr(0) & (1<<8)) && --d); /* wait IDON */

    /* START */
    pcnet_write_csr(0, CSR0_STRT | (1<<8) /* clear IDON */);
    printk(T,"pcnet: ready\n");
    return 0;
}

static int pcnet_send(const void *buf, u32 len) {
    if (!pc_iobase || len > PKT_SZ) return -1;
    u32 i = tx_cur;
    if (txd[i].status & DESC_OWN) return -1; /* busy */
    memcpy(tx_buf[i], buf, len);
    if (len < 60) { memset(tx_buf[i]+len,0,60-len); len=60; }
    txd[i].buf_len = (i16)(-(i16)len);
    txd[i].status  = DESC_OWN | DESC_STP | DESC_ENP;
    tx_cur = (i+1) % TX_RING;
    pcnet_write_csr(0, pcnet_read_csr(0) | CSR0_TX);
    return 0;
}

static int pcnet_recv(void *buf, u32 maxlen) {
    if (!pc_iobase) return 0;
    u32 i = rx_cur;
    if (rxd[i].status & DESC_OWN) return 0;
    u32 len = rxd[i].msg_len & 0xFFF;
    if (len > maxlen) len = maxlen;
    memcpy(buf, rx_buf[i], len);
    rxd[i].status = DESC_OWN;
    rx_cur = (i+1) % RX_RING;
    return (int)len;
}

static int pcnet_get_mac(u8 mac[6]) {
    if (!pc_iobase) return -1;
    for (int i=0;i<6;i++) {
        u8 b; __asm__("inb %1,%0":"=a"(b):"Nd"((u16)(pc_iobase+i))); mac[i]=b;
    }
    return 0;
}

static net_driver_t pcnet_driver = {
    .name="pcnet", .probe=pcnet_probe, .init=pcnet_init,
    .send=pcnet_send, .recv=pcnet_recv, .get_mac=pcnet_get_mac, .next=NULL,
};

void pcnet_register(void) { net_register_driver(&pcnet_driver); }
net_driver_t *pcnet_get_driver(void) { return &pcnet_driver; }
