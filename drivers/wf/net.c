/*
 * net.c — PCI-scan-first network init for Ark kernel
 *
 * Flow:
 *   print_eth_devices() [called from init.c]
 *     └─ detect_eth()  — walks PCI bus ONCE, fills eth_devs[]
 *
 *   ip_init() → net_init_all()
 *     └─ reads eth_devs[] via eth_get_dev()  (NO second PCI walk)
 *        matches vendor:device → calls only that driver's init
 *
 * Adding a new NIC: add PCI IDs to nic_ids[] and implement foo_get_driver().
 */

#include "ark/net.h"
#include "ark/pci.h"
#include "ark/printk.h"
#include "ark/kconfig.h"

net_driver_t *g_net_driver = NULL;

/* Legacy compat */
static net_driver_t *_legacy_list = NULL;
void net_register_driver(net_driver_t *drv) {
    if (!drv) return;
    drv->next    = _legacy_list;
    _legacy_list = drv;
}

/* eth-dev.c accessors — avoids second PCI scan */
extern u8 eth_get_count(void);
extern u8 eth_get_dev(u8 idx, u16 *vid, u16 *did, u8 *bus, u8 *slot, u8 *func);

/* Each driver exposes a _get_driver() returning its static net_driver_t */
#if CONFIG_E1000_ENABLE
extern net_driver_t *e1000_get_driver(void);
#endif
#if CONFIG_NET_ENABLE
extern net_driver_t *rtl8139_get_driver(void);
extern net_driver_t *virtio_net_get_driver(void);
extern net_driver_t *pcnet_get_driver(void);
#endif

/* ── PCI ID → driver table ───────────────────────────────────────── */
typedef struct {
    u16  vendor;
    u16  device;
    net_driver_t *(*get_drv)(void);
    const char   *desc;
} nic_id_t;

static nic_id_t nic_ids[] = {
#if CONFIG_E1000_ENABLE
    { 0x8086, 0x100E, e1000_get_driver, "Intel 82540EM (e1000)"  },
    { 0x8086, 0x100F, e1000_get_driver, "Intel 82545EM (e1000)"  },
    { 0x8086, 0x10D3, e1000_get_driver, "Intel 82574L (e1000e)"  },
    { 0x8086, 0x1533, e1000_get_driver, "Intel i210 (igb)"       },
    { 0x8086, 0x1502, e1000_get_driver, "Intel 82579LM (e1000e)" },
    { 0x8086, 0x1503, e1000_get_driver, "Intel 82579V (e1000e)"  },
    { 0x8086, 0x15B8, e1000_get_driver, "Intel I219-V (e1000e)"  },
    { 0x8086, 0x10F5, e1000_get_driver, "Intel 82567LM (e1000e)" },
#endif
#if CONFIG_NET_ENABLE
    { 0x10EC, 0x8139, rtl8139_get_driver,    "Realtek RTL8139"       },
    { 0x1AF4, 0x1000, virtio_net_get_driver, "VirtIO-net (legacy)"   },
    { 0x1AF4, 0x1041, virtio_net_get_driver, "VirtIO-net (modern)"   },
    { 0x1022, 0x2000, pcnet_get_driver,      "AMD PCnet-PCI II"      },
    { 0x1022, 0x2001, pcnet_get_driver,      "AMD PCnet-PCI III"     },
#endif
    { 0x0000, 0x0000, NULL, NULL }
};

/* ── net_init_all ────────────────────────────────────────────────── */
void net_init_all(void) {
    /* detect_eth() was already called by print_eth_devices() in init.c.
     * eth_get_count() calls it only if the cache is still empty.      */
    u8 count = eth_get_count();

    if (count == 0) {
        printk(T, "net: no Ethernet controllers on PCI bus\n");
        return;
    }

    printk(T, "net: %u NIC(s) found — matching driver...\n", (u32)count);

    for (u8 n = 0; n < count; n++) {
        u16 vid, did;
        u8  bus, slot, func;
        if (!eth_get_dev(n, &vid, &did, &bus, &slot, &func)) continue;

        /* Walk ID table */
        for (int i = 0; nic_ids[i].get_drv; i++) {
            if (nic_ids[i].vendor != vid) continue;
            if (nic_ids[i].device != did) continue;

            net_driver_t *drv = nic_ids[i].get_drv();
            printk(T, "net: [%02x:%02x.%x] %s → '%s'\n",
                   (u32)bus, (u32)slot, (u32)func,
                   nic_ids[i].desc, drv->name);

            if (drv->init && drv->init() == 0) {
                g_net_driver = drv;
                printk(T, "net: driver '%s' active\n", drv->name);
                return;
            }
            printk(T, "net: '%s' init failed — trying next NIC\n", drv->name);
            break;
        }

        if (!g_net_driver)
            printk(T, "net: %04x:%04x at [%02x:%02x.%x] — no driver\n",
                   (u32)vid, (u32)did, (u32)bus, (u32)slot, (u32)func);
    }

    if (!g_net_driver)
        printk(T, "net: no supported NIC initialised\n");
}

int net_send(const void *buf, u32 len) {
    if (g_net_driver && g_net_driver->send)
        return g_net_driver->send(buf, len);
    return -1;
}

int net_recv(void *buf, u32 maxlen) {
    if (g_net_driver && g_net_driver->recv)
        return g_net_driver->recv(buf, maxlen);
    return 0;
}

int net_get_mac(u8 mac[6]) {
    if (g_net_driver && g_net_driver->get_mac)
        return g_net_driver->get_mac(mac);
    return -1;
}
