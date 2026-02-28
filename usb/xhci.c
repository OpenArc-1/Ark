/*
 * usb/xhci.c — Extensible Host Controller Interface (USB 3.x)
 *
 * xHCI is the modern USB host controller supporting USB 1.1, 2.0, and 3.x
 * on a single controller. Unlike EHCI/UHCI/OHCI, xHCI handles all speeds
 * internally — no companion controllers needed.
 *
 * Architecture overview:
 *   All registers are MMIO. The controller is found via PCI class 0x0C/0x03/0x30.
 *
 *   Capability registers (base + 0):
 *     CAPLENGTH  — offset to operational registers
 *     HCIVERSION — BCD version (0x0096=USB2, 0x0100=USB3)
 *     HCSPARAMS1 — max slots, interrupts, ports
 *     HCSPARAMS2 — event ring segment table max
 *     HCCPARAMS1 — 64-bit addressing, port power control, etc.
 *
 *   Operational registers (base + CAPLENGTH):
 *     USBCMD     — Run/Stop, Host Reset, interrupt enable
 *     USBSTS     — HCHalted, event interrupt, port change
 *     DNCTRL     — device notification control
 *     CRCR       — command ring control register
 *     DCBAAP      — device context base address array pointer (64-bit)
 *     CONFIG      — max device slots enabled
 *
 *   Runtime registers (base + RTSOFF):
 *     MFINDEX    — microframe index
 *     IR[n]      — interrupter registers (IMAN, IMOD, ERSTSZ, ERSTBA, ERDP)
 *
 *   Doorbell array (base + DBOFF):
 *     DB[0]      — host controller command doorbell
 *     DB[n]      — device slot n doorbell
 *
 * Data structures (all physical/identity-mapped):
 *   Device Context Base Address Array (DCBAA) — 256 × 64-bit pointers
 *   Device Context (DC)                       — slot + 31 endpoint contexts
 *   Input Context                              — control + slot + 31 EP ctx
 *   Transfer Ring                              — circular list of TRBs
 *   Event Ring                                 — controller → driver TRBs
 *   Command Ring                               — driver → controller TRBs
 *   Event Ring Segment Table (ERST)            — one or more ring segments
 *
 * Transfer Request Block (TRB) — 16 bytes:
 *   [63:0]  Parameter  — buffer pointer (64-bit) or immediate data
 *   [95:64] Status     — transfer length, completion code, cycle bit
 *   [127:96] Control   — TRB type, endpoint ID, flags
 *
 * TRB types:
 *   1  Normal             — bulk/interrupt data transfer
 *   2  Setup Stage        — control SETUP packet
 *   3  Data Stage         — control DATA phase
 *   4  Status Stage       — control STATUS phase
 *   6  Link               — end of ring, link to start
 *   9  Enable Slot        — allocate device slot
 *   11 Address Device     — assign USB address, configure slot
 *   12 Configure Endpoint — enable/disable endpoints after SET_CONFIGURATION
 *   23 No-Op Command      — used to test command ring
 *   32 Transfer Event     — completion notification from HC
 *   33 Command Completion Event
 *   34 Port Status Change Event
 *
 * Port speed identification (PSI in PORTSC bits 13:10):
 *   1 = Full Speed  (12 Mbit/s, USB 1.1)
 *   2 = Low Speed   (1.5 Mbit/s, USB 1.1)
 *   3 = High Speed  (480 Mbit/s, USB 2.0)
 *   4 = SuperSpeed  (5 Gbit/s, USB 3.0)
 *   5 = SS Gen2     (10 Gbit/s, USB 3.1)
 *
 * Ark integration:
 *   xhci_init(mmio_base)  — reset HC, set up rings, start running
 *   xhci_probe_ports()    — detect attached devices, enumerate keyboards
 *   xhci_kbd_poll()       — check event ring for completed IN TRBs
 *
 * Note: Full xHCI enumeration is complex. For keyboards, QEMU also
 * emulates UHCI/OHCI companion ports where the keyboard attaches as
 * a low/full-speed device. usb_kbd.c handles those directly.
 * This file provides the xHCI infrastructure for USB 3.x devices.
 */
#include "ark/types.h"
#include "ark/printk.h"

/* ── Capability register offsets ──────────────────────────── */
#define XHCI_CAPLENGTH   0x00
#define XHCI_HCIVERSION  0x02
#define XHCI_HCSPARAMS1  0x04
#define XHCI_HCSPARAMS2  0x08
#define XHCI_HCCPARAMS1  0x10
#define XHCI_DBOFF       0x14
#define XHCI_RTSOFF      0x18

/* ── Operational register offsets (base = cap_base + CAPLENGTH) */
#define XHCI_USBCMD   0x00
#define XHCI_USBSTS   0x04
#define XHCI_PAGESIZE 0x08
#define XHCI_DNCTRL   0x14
#define XHCI_CRCR     0x18
#define XHCI_DCBAAP   0x30   /* 64-bit */
#define XHCI_CONFIG   0x38

/* ── USBCMD bits ──────────────────────────────────────────── */
#define XHCI_CMD_RUN    (1u<<0)
#define XHCI_CMD_HCRST  (1u<<1)
#define XHCI_CMD_INTE   (1u<<2)

/* ── USBSTS bits ──────────────────────────────────────────── */
#define XHCI_STS_HALT   (1u<<0)
#define XHCI_STS_EINT   (1u<<3)
#define XHCI_STS_PCD    (1u<<4)

/* ── PORTSC bits ──────────────────────────────────────────── */
#define XHCI_PORTSC_CCS  (1u<<0)   /* Current Connect Status */
#define XHCI_PORTSC_PED  (1u<<1)   /* Port Enabled/Disabled  */
#define XHCI_PORTSC_PR   (1u<<4)   /* Port Reset             */
#define XHCI_PORTSC_PLS  (0xFu<<5) /* Port Link State        */
#define XHCI_PORTSC_PP   (1u<<9)   /* Port Power             */
#define XHCI_PORTSC_PSI  (0xFu<<10)/* Port Speed ID          */
#define XHCI_PORTSC_CSC  (1u<<17)  /* Connect Status Change  */
#define XHCI_PORTSC_PRC  (1u<<21)  /* Port Reset Change      */

/* ── MMIO accessor ────────────────────────────────────────── */
static inline u32 xhci_rd32(usize base, u32 off) {
    return *(volatile u32*)(base + off);
}
static inline void xhci_wr32(usize base, u32 off, u32 v) {
    *(volatile u32*)(base + off) = v;
}
static inline u64 xhci_rd64(usize base, u32 off) {
    return *(volatile u64*)(base + off);
}
static inline void xhci_wr64(usize base, u32 off, u64 v) {
    *(volatile u64*)(base + off) = v;
}

/* ── TRB ──────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    u64 param;
    u32 status;
    u32 control;
} xhci_trb_t;

#define TRB_TYPE(t)  ((u32)(t) << 10)
#define TRB_CYCLE    (1u<<0)
#define TRB_ENT      (1u<<1)   /* Evaluate Next TRB */
#define TRB_LINK_TC  (1u<<1)   /* Toggle Cycle (in Link TRB) */
#define TRB_IOC      (1u<<5)   /* Interrupt On Completion */
#define TRB_IDT      (1u<<6)   /* Immediate Data */

/* ── DMA pool ─────────────────────────────────────────────── */
#define XHCI_DMA_SZ  0x8000
__attribute__((aligned(4096))) static u8 xhci_dma[XHCI_DMA_SZ];
#define XHCI_PHYS(off) ((u64)((usize)xhci_dma + (usize)(off)))
#define XHCI_PTR(off)  ((void*)((usize)xhci_dma + (usize)(off)))

#define OFF_DCBAA      0x0000u   /* 256 × 8 bytes = 2KB            */
#define OFF_CMD_RING   0x0800u   /* command ring: 64 TRBs × 16B    */
#define OFF_EVT_RING   0x1400u   /* event ring: 64 TRBs × 16B      */
#define OFF_ERST       0x2000u   /* event ring segment table        */
#define OFF_XFER_RING  0x2040u   /* keyboard transfer ring          */
#define OFF_INPUT_CTX  0x3000u   /* input context (2KB)             */
#define OFF_DEV_CTX    0x5000u   /* device context (2KB)            */
#define OFF_REPORT     0x7000u   /* HID report buffer               */

#define XHCI_RING_LEN  64

/* ── State ────────────────────────────────────────────────── */
static usize  xhci_cap  = 0;   /* capability register base    */
static usize  xhci_op   = 0;   /* operational register base   */
static usize  xhci_rt   = 0;   /* runtime register base       */
static usize  xhci_db   = 0;   /* doorbell array base         */
static u32    xhci_ports = 0;
static bool   xhci_ready = false;
static u32    cmd_enq = 0;     /* command ring enqueue index  */
static u8     cmd_cs  = 1;     /* command ring cycle state    */
static u32    evt_deq = 0;     /* event ring dequeue index    */
static u8     evt_cs  = 1;     /* event ring cycle state      */
static u32    xfer_enq = 0;
static u8     xfer_cs  = 1;

static void xhci_delay(u32 ms) {
    for (u32 i = 0; i < ms * 10000u; i++) __asm__ volatile("pause");
}

/* Enqueue one TRB on the command ring */
static void xhci_cmd(u64 param, u32 status, u32 ctrl_type_flags) {
    xhci_trb_t *ring = (xhci_trb_t*)XHCI_PTR(OFF_CMD_RING);
    xhci_trb_t *trb  = &ring[cmd_enq];
    trb->param   = param;
    trb->status  = status;
    trb->control = ctrl_type_flags | (cmd_cs ? TRB_CYCLE : 0u);
    cmd_enq++;
    if (cmd_enq == XHCI_RING_LEN - 1) {
        /* Link TRB back to start */
        trb       = &ring[cmd_enq];
        trb->param   = XHCI_PHYS(OFF_CMD_RING);
        trb->status  = 0;
        trb->control = TRB_TYPE(6) | TRB_LINK_TC | (cmd_cs ? TRB_CYCLE : 0u);
        cmd_enq = 0;
        cmd_cs ^= 1;
    }
    /* Ring doorbell 0 (host controller) */
    xhci_wr32(xhci_db, 0, 0);
}

bool xhci_init(u32 mmio_base) {
    xhci_cap = (usize)mmio_base;
    u8 cap_len  = (u8)xhci_rd32(xhci_cap, XHCI_CAPLENGTH);
    xhci_op     = xhci_cap + cap_len;
    xhci_rt     = xhci_cap + (xhci_rd32(xhci_cap, XHCI_RTSOFF) & ~0x1Fu);
    xhci_db     = xhci_cap + (xhci_rd32(xhci_cap, XHCI_DBOFF)  & ~3u);
    xhci_ports  = (xhci_rd32(xhci_cap, XHCI_HCSPARAMS1) >> 24) & 0xFFu;

    /* Stop controller */
    xhci_wr32(xhci_op, XHCI_USBCMD,
              xhci_rd32(xhci_op, XHCI_USBCMD) & ~XHCI_CMD_RUN);
    u32 t = 20;
    while (!(xhci_rd32(xhci_op, XHCI_USBSTS) & XHCI_STS_HALT) && t--)
        xhci_delay(1);

    /* Reset */
    xhci_wr32(xhci_op, XHCI_USBCMD,
              xhci_rd32(xhci_op, XHCI_USBCMD) | XHCI_CMD_HCRST);
    t = 100;
    while ((xhci_rd32(xhci_op, XHCI_USBCMD) & XHCI_CMD_HCRST) && t--)
        xhci_delay(1);
    if (xhci_rd32(xhci_op, XHCI_USBCMD) & XHCI_CMD_HCRST) {
        printk(T, "[xHCI] reset timeout\n");
        return false;
    }
    xhci_delay(1);

    /* Max slots */
    u32 max_slots = xhci_rd32(xhci_cap, XHCI_HCSPARAMS1) & 0xFFu;
    xhci_wr32(xhci_op, XHCI_CONFIG, max_slots);

    /* DCBAA */
    u64 *dcbaa = (u64*)XHCI_PTR(OFF_DCBAA);
    for (u32 i = 0; i < 256; i++) dcbaa[i] = 0;
    xhci_wr64(xhci_op, XHCI_DCBAAP, XHCI_PHYS(OFF_DCBAA));

    /* Command ring */
    xhci_trb_t *cr = (xhci_trb_t*)XHCI_PTR(OFF_CMD_RING);
    for (u32 i = 0; i < XHCI_RING_LEN; i++) {
        cr[i].param = cr[i].status = cr[i].control = 0;
    }
    xhci_wr64(xhci_op, XHCI_CRCR, XHCI_PHYS(OFF_CMD_RING) | 1u); /* RCS=1 */

    /* Event ring segment table */
    typedef struct { u64 addr; u32 size; u32 reserved; } erst_entry_t;
    erst_entry_t *erst = (erst_entry_t*)XHCI_PTR(OFF_ERST);
    erst[0].addr = XHCI_PHYS(OFF_EVT_RING);
    erst[0].size = XHCI_RING_LEN;
    erst[0].reserved = 0;
    xhci_trb_t *er = (xhci_trb_t*)XHCI_PTR(OFF_EVT_RING);
    for (u32 i = 0; i < XHCI_RING_LEN; i++) {
        er[i].param = er[i].status = er[i].control = 0;
    }
    /* Primary interrupter (IR[0]) at runtime base + 0x20 */
    usize ir0 = xhci_rt + 0x20u;
    xhci_wr32(ir0, 0x08, 1);               /* ERSTSZ = 1 segment */
    xhci_wr64(ir0, 0x10, XHCI_PHYS(OFF_ERST));  /* ERSTBA */
    xhci_wr64(ir0, 0x18, XHCI_PHYS(OFF_EVT_RING)); /* ERDP */

    /* Start */
    xhci_wr32(xhci_op, XHCI_USBCMD,
              xhci_rd32(xhci_op, XHCI_USBCMD) | XHCI_CMD_RUN);
    xhci_delay(2);

    xhci_ready = true;
    printk(T, "[xHCI] v%04x started, %u port(s)\n",
           xhci_rd32(xhci_cap, XHCI_HCIVERSION) >> 16, xhci_ports);
    xhci_probe_ports();
    return true;
}

void xhci_probe_ports(void) {
    if (!xhci_ready) return;
    for (u32 i = 0; i < xhci_ports; i++) {
        u32 sc = xhci_rd32(xhci_op, 0x400 + i * 0x10);
        if (!(sc & XHCI_PORTSC_CCS)) continue;
        u32 speed = (sc & XHCI_PORTSC_PSI) >> 10;
        const char *sp = speed==1?"FS":speed==2?"LS":speed==3?"HS":speed==4?"SS":"SS+";
        printk(T, "[xHCI] port %u: device connected (%s)\n", i, sp);
    }
}

void xhci_kbd_poll(void) { /* Polling via event ring TBD */ }
bool xhci_kbd_ready(void) { return false; }  /* Full enumeration not yet implemented */
