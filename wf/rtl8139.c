/*
 * rtl8139.c - stub for Realtek RTL8139 NIC driver
 *
 * This is placeholder code; the real driver would perform PCI probing,
 * map I/O/MMIO regions, and implement send/receive functions.  Here we
 * simply register a driver entry so the net layer can see it.
 */

#include "ark/net.h"
#include "ark/printk.h"

static int rtl8139_probe(void) {
    /* For now return non-zero to indicate not present */
    return -1;
}

static int rtl8139_init(void) {
    printk(T,"rtl8139: initialization stub (no hardware)\n");
    return 0;
}

static int rtl8139_send(const void *buf, u32 len) {
    /* not implemented */
    return -1;
}

static int rtl8139_recv(void *buf, u32 maxlen) {
    return 0;
}

static int rtl8139_get_mac(u8 mac[6]) {
    return -1;
}

static net_driver_t rtl8139_driver = {
    .name = "rtl8139",
    .probe = rtl8139_probe,
    .init  = rtl8139_init,
    .send  = rtl8139_send,
    .recv  = rtl8139_recv,
    .get_mac = rtl8139_get_mac,
    .next = NULL,
};

static void __attribute__((constructor)) register_rtl8139(void) {
    net_register_driver(&rtl8139_driver);
}
