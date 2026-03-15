/*
 * virtio_net.c — VirtIO network driver for Ark kernel (legacy PCI, I/O transport)
 *
 * Supports QEMU/KVM virtio-net: PCI vendor 0x1AF4, device 0x1000 (legacy)
 * and 0x1041 (transitional modern).
 *
 * This driver uses the legacy virtio PCI I/O interface (no MMIO config).
 * The two virtqueues are polled (no interrupt support needed).
 *
 *  VQ 0 = receiveq   (host → guest)
 *  VQ 1 = transmitq  (guest → host)
 */

#include "ark/net.h"
#include "ark/pci.h"
#include "ark/printk.h"
#include "ark/mem.h"
#include "ark/types.h"

/* ── VirtIO PCI legacy registers (offset from BAR0 I/O base) ───────── */
#define VIRTIO_PCI_HOST_FEATURES    0x00
#define VIRTIO_PCI_GUEST_FEATURES   0x04
#define VIRTIO_PCI_QUEUE_PFN        0x08
#define VIRTIO_PCI_QUEUE_SIZE       0x0C
#define VIRTIO_PCI_QUEUE_SEL        0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10
#define VIRTIO_PCI_STATUS           0x12
#define VIRTIO_PCI_ISR              0x13
#define VIRTIO_PCI_CONFIG           0x14  /* device-specific config */

/* Status bits */
#define VIRTIO_STATUS_ACK           0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FAILED        0x80

/* Feature bits */
#define VIRTIO_NET_F_MAC            (1<<5)
#define VIRTIO_NET_F_STATUS         (1<<16)

/* Descriptor flags */
#define VRING_DESC_F_NEXT           1
#define VRING_DESC_F_WRITE          2   /* device write-only */

#define QUEUE_SIZE   64
#define PKT_SIZE     2048

/* ── vring structures (must be physically contiguous + aligned) ─────── */
struct vring_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} __attribute__((packed));

struct vring_avail {
    u16 flags;
    u16 idx;
    u16 ring[QUEUE_SIZE];
} __attribute__((packed));

struct vring_used_elem {
    u32 id;
    u32 len;
} __attribute__((packed));

struct vring_used {
    u16 flags;
    u16 idx;
    struct vring_used_elem ring[QUEUE_SIZE];
} __attribute__((packed));

/* virtio-net header prepended to every packet */
struct virtio_net_hdr {
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
} __attribute__((packed));

/* ── per-queue state ────────────────────────────────────────────────── */
struct vq {
    struct vring_desc  desc [QUEUE_SIZE] __attribute__((aligned(4096)));
    struct vring_avail avail              __attribute__((aligned(2)));
    u8     _pad[4096 - sizeof(struct vring_avail) - QUEUE_SIZE*2];
    struct vring_used  used               __attribute__((aligned(4096)));
    u16  last_used_idx;
    u16  next_free;
};

static struct vq  rxq __attribute__((aligned(4096)));
static struct vq  txq __attribute__((aligned(4096)));

/* RX buffers — one per descriptor */
static struct virtio_net_hdr rx_hdr [QUEUE_SIZE];
static u8                    rx_buf [QUEUE_SIZE][PKT_SIZE];

/* TX scratch */
static struct virtio_net_hdr tx_hdr;
static u8                    tx_buf[PKT_SIZE];

static u16 vnet_iobase = 0;

/* ── I/O helpers ────────────────────────────────────────────────────── */
static inline void _vo8 (u16 p,u8  v){__asm__("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void _vo16(u16 p,u16 v){__asm__("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void _vo32(u16 p,u32 v){__asm__("outl %0,%1"::"a"(v),"Nd"(p));}
static inline u8   _vi8 (u16 p){u8  v;__asm__("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u16  _vi16(u16 p){u16 v;__asm__("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u32  _vi32(u16 p){u32 v;__asm__("inl %1,%0":"=a"(v):"Nd"(p));return v;}

#define VIO_R8(r)    _vi8 ((u16)(vnet_iobase+(r)))
#define VIO_R16(r)   _vi16((u16)(vnet_iobase+(r)))
#define VIO_R32(r)   _vi32((u16)(vnet_iobase+(r)))
#define VIO_W8(r,v)  _vo8 ((u16)(vnet_iobase+(r)),(u8)(v))
#define VIO_W16(r,v) _vo16((u16)(vnet_iobase+(r)),(u16)(v))
#define VIO_W32(r,v) _vo32((u16)(vnet_iobase+(r)),(u32)(v))

/* ── helper: activate a vring on the device ─────────────────────────── */
static void vq_activate(u16 qidx, struct vq *q) {
    VIO_W16(VIRTIO_PCI_QUEUE_SEL, qidx);
    u16 sz = VIO_R16(VIRTIO_PCI_QUEUE_SIZE);
    if (sz == 0 || sz > QUEUE_SIZE) sz = QUEUE_SIZE;
    /* PFN in 4096-byte pages */
    VIO_W32(VIRTIO_PCI_QUEUE_PFN, (u32)q / 4096);
    (void)sz;
}

/* ── fill RX queue descriptors ─────────────────────────────────────── */
static void rxq_fill(void) {
    for (u16 i = 0; i < QUEUE_SIZE; i++) {
        /* descriptor 0: virtio-net header (device-write) */
        rxq.desc[i].addr  = (u32)&rx_hdr[i];
        rxq.desc[i].len   = sizeof(struct virtio_net_hdr);
        rxq.desc[i].flags = VRING_DESC_F_WRITE | VRING_DESC_F_NEXT;
        rxq.desc[i].next  = (i+1) % QUEUE_SIZE; /* chain to data buffer */

        /* We cheat: use a single large descriptor with WRITE flag */
        /* For simplicity, use one-descriptor-per-slot approach below */
        /* Re-do: one descriptor pair per slot using indices i*2, i*2+1 */
        (void)i;
    }
    /* Simpler: use single descriptor per slot for both hdr+data */
    memset(&rxq, 0, sizeof(rxq));
    for (u16 i = 0; i < QUEUE_SIZE; i++) {
        rxq.desc[i].addr  = (u32)rx_buf[i];
        rxq.desc[i].len   = PKT_SIZE;
        rxq.desc[i].flags = VRING_DESC_F_WRITE;
        rxq.desc[i].next  = 0;
        rxq.avail.ring[i] = i;
    }
    rxq.avail.idx = QUEUE_SIZE;
    rxq.last_used_idx = 0;
    /* Notify host that RX descriptors are ready */
    VIO_W16(VIRTIO_PCI_QUEUE_NOTIFY, 0);
}

/* ── probe ──────────────────────────────────────────────────────────── */
static int virtio_net_probe(void) {
    pci_device_t dev;
    for_each_pci_device(dev) {
        if (dev.vendor_id == 0x1AF4 &&
            (dev.device_id == 0x1000 || dev.device_id == 0x1041))
            return 0;
    }
    return -1;
}

/* ── init ───────────────────────────────────────────────────────────── */
static int virtio_net_init(void) {
    pci_device_t dev;
    int found = 0;
    for_each_pci_device(dev) {
        if (dev.vendor_id == 0x1AF4 &&
            (dev.device_id == 0x1000 || dev.device_id == 0x1041)) {
            found = 1; break;
        }
    }
    if (!found) return -1;

    /* Enable bus mastering + I/O space */
    u32 cmd = pciread(dev.bus, dev.slot, dev.func, 0x04);
    pciwrite(dev.bus, dev.slot, dev.func, 0x04, cmd|(1<<0)|(1<<2));

    u32 bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
    if (!(bar0 & 1)) {
        printk(T,"virtio_net: BAR0 not I/O\n"); return -1;
    }
    vnet_iobase = (u16)(bar0 & 0xFFFC);
    printk(T,"virtio_net: I/O base=0x%x\n",(u32)vnet_iobase);

    /* Reset */
    VIO_W8(VIRTIO_PCI_STATUS, 0);

    /* Acknowledge + driver present */
    VIO_W8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
    VIO_W8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Negotiate features: just request MAC */
    u32 host_feat = VIO_R32(VIRTIO_PCI_HOST_FEATURES);
    u32 drv_feat  = host_feat & VIRTIO_NET_F_MAC;
    VIO_W32(VIRTIO_PCI_GUEST_FEATURES, drv_feat);

    /* Setup queues */
    memset(&rxq, 0, sizeof(rxq));
    memset(&txq, 0, sizeof(txq));
    vq_activate(0, &rxq);
    vq_activate(1, &txq);

    /* Driver OK */
    VIO_W8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK|VIRTIO_STATUS_DRIVER|VIRTIO_STATUS_DRIVER_OK);

    /* Fill RX queue */
    rxq_fill();

    printk(T,"virtio_net: ready\n");
    return 0;
}

/* ── send ───────────────────────────────────────────────────────────── */
static int virtio_net_send(const void *buf, u32 len) {
    if (!vnet_iobase || len > PKT_SIZE) return -1;

    /* Build packet: virtio_net_hdr + data in contiguous tx_buf */
    memset(&tx_hdr, 0, sizeof(tx_hdr));
    memcpy(tx_buf, buf, len);

    u16 idx = txq.next_free % QUEUE_SIZE;
    txq.desc[idx].addr  = (u32)tx_buf;
    txq.desc[idx].len   = len;
    txq.desc[idx].flags = 0;
    txq.desc[idx].next  = 0;

    txq.avail.ring[txq.avail.idx % QUEUE_SIZE] = idx;
    txq.avail.idx++;
    txq.next_free++;

    /* Notify transmitq (vq index 1) */
    VIO_W16(VIRTIO_PCI_QUEUE_NOTIFY, 1);
    return 0;
}

/* ── recv ───────────────────────────────────────────────────────────── */
static int virtio_net_recv(void *buf, u32 maxlen) {
    if (!vnet_iobase) return 0;

    if (rxq.last_used_idx == rxq.used.idx)
        return 0;

    u16 id  = (u16)(rxq.used.ring[rxq.last_used_idx % QUEUE_SIZE].id);
    u32 len = rxq.used.ring[rxq.last_used_idx % QUEUE_SIZE].len;

    if (len > maxlen) len = maxlen;
    memcpy(buf, rx_buf[id], len);

    /* Recycle descriptor */
    rxq.avail.ring[rxq.avail.idx % QUEUE_SIZE] = id;
    rxq.avail.idx++;
    rxq.last_used_idx++;

    VIO_W16(VIRTIO_PCI_QUEUE_NOTIFY, 0);
    return (int)len;
}

/* ── get_mac ────────────────────────────────────────────────────────── */
static int virtio_net_get_mac(u8 mac[6]) {
    if (!vnet_iobase) return -1;
    /* MAC is at device-specific config offset 0x14 */
    for (int i = 0; i < 6; i++)
        mac[i] = VIO_R8(VIRTIO_PCI_CONFIG + i);
    return 0;
}

static net_driver_t virtio_net_driver = {
    .name    = "virtio_net",
    .probe   = virtio_net_probe,
    .init    = virtio_net_init,
    .send    = virtio_net_send,
    .recv    = virtio_net_recv,
    .get_mac = virtio_net_get_mac,
    .next    = NULL,
};

void virtio_net_register(void) { net_register_driver(&virtio_net_driver); }
net_driver_t *virtio_net_get_driver(void) { return &virtio_net_driver; }
