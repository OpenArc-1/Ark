/*
 * usb/ehci.c — Enhanced Host Controller Interface (USB 2.0)
 *
 * EHCI handles High-Speed (480 Mbit/s) USB 2.0 devices.
 * Low/Full-speed devices on an EHCI controller are handed off to companion
 * UHCI or OHCI controllers via the "Port Owner" bit in each port's PORTSC.
 *
 * Architecture:
 *   Capability registers (MMIO, base + 0x00) — read-only HW parameters
 *   Operational registers (MMIO, base + CAPLENGTH) — runtime control
 *   Periodic Frame List — 1024 entries, each 4KB-aligned physical pointer
 *   Async List — circular linked list of Queue Heads (QH)
 *
 * Key operational registers (offset from Op base):
 *   +0x00  USBCMD    — Run/Stop, Host Reset, periodic/async schedule enable
 *   +0x04  USBSTS    — interrupt status
 *   +0x08  USBINTR   — interrupt enable mask
 *   +0x0C  FRINDEX   — current frame index
 *   +0x14  PERIODICLISTBASE — physical address of frame list
 *   +0x18  ASYNCLISTADDR    — physical address of async QH (circular)
 *   +0x40  CONFIGFLAG  — 1 = all ports routed to EHCI, 0 = companion
 *   +0x44  PORTSC[n]   — per-port status/control
 *
 * Ark integration:
 *   ehci_init(mmio_base)  — discover ports, hand off LS/FS to companion HCs
 *   ehci_scan_ports()     — detect and classify attached devices by speed
 *
 * Note: EHCI does not handle keyboards directly.
 * Low/Full-speed USB keyboards are automatically handed off to UHCI/OHCI
 * companion controllers by setting the Port Owner bit (bit 13 of PORTSC).
 */
#include "ark/types.h"
#include "ark/printk.h"
#include "ark/mmio.h"

typedef struct {
    u8  caplength;
    u8  reserved;
    u16 hciversion;
    u32 hcsparams;
    u32 hccparams;
    u32 hcspportroute;
} __attribute__((packed)) ehci_cap_t;

typedef struct {
    volatile u32 usbcmd;
    volatile u32 usbsts;
    volatile u32 usbintr;
    volatile u32 frindex;
    volatile u32 ctrldssegment;
    volatile u32 periodiclistbase;
    volatile u32 asynclistaddr;
    u32 _reserved[9];
    volatile u32 configflag;
    volatile u32 portsc[16];
} __attribute__((packed)) ehci_op_t;

static ehci_cap_t *ehci_cap = 0;
static ehci_op_t  *ehci_op  = 0;
static int         ehci_ports = 0;
static bool        ehci_running = false;

static void ehci_delay(u32 ms){ for(u32 i=0;i<ms*10000u;i++) __asm__ volatile("pause"); }

bool ehci_init(u32 mmio_base) {
    ehci_cap = (ehci_cap_t*)(usize)mmio_base;
    ehci_op  = (ehci_op_t*)((u8*)(usize)mmio_base + ehci_cap->caplength);
    ehci_ports = ehci_cap->hcsparams & 0x0Fu;
    if (ehci_ports == 0 || ehci_ports > 15) ehci_ports = 2;

    /* Stop controller */
    ehci_op->usbcmd &= ~(1u<<0);
    ehci_delay(5);

    /* Reset */
    ehci_op->usbcmd |= (1u<<1);
    u32 t = 50;
    while ((ehci_op->usbcmd & (1u<<1)) && t--) ehci_delay(1);

    /* Route all ports to EHCI */
    ehci_op->configflag = 1u;
    ehci_delay(5);

    /* Start */
    ehci_op->usbcmd |= (1u<<0);
    ehci_running = true;

    printk(T, "[EHCI] started, %d port(s)\n", ehci_ports);
    ehci_scan_ports();
    return true;
}

void ehci_scan_ports(void) {
    if (!ehci_running) return;
    for (int i = 0; i < ehci_ports; i++) {
        u32 sc = ehci_op->portsc[i];
        if (!(sc & 0x01u)) continue;   /* no device */

        /* Reset port */
        ehci_op->portsc[i] = (sc & ~0x2Au) | (1u<<8);
        ehci_delay(50);
        ehci_op->portsc[i] &= ~(1u<<8);
        ehci_delay(10);

        sc = ehci_op->portsc[i];
        if (sc & (1u<<2)) {
            /* High-speed: port enabled by EHCI */
            printk(T, "[EHCI] port %d: high-speed device\n", i);
        } else {
            /* Low/Full-speed: hand off to companion controller */
            printk(T, "[EHCI] port %d: FS/LS device -> companion\n", i);
            ehci_op->portsc[i] |= (1u<<13); /* Port Owner = companion */
        }
    }
}

bool ehci_running_state(void) { return ehci_running; }
