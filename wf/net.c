
#include "ark/net.h"
#include "ark/printk.h"

/* simple singly‑linked list of registered drivers */
static net_driver_t *drivers = NULL;
net_driver_t *g_net_driver = NULL;

void net_register_driver(net_driver_t *drv) {
    if (!drv) return;
    drv->next = drivers;
    drivers = drv;
    printk(T,"net: registered driver %s\n", drv->name);
}

void net_init_all(void) {
    net_driver_t *drv = drivers;
    while (drv) {
        if (drv->probe && drv->probe() == 0) {
            printk(T,"net: probing driver %s succeeded\n", drv->name);
            if (drv->init)
                drv->init();
            g_net_driver = drv;
            printk(T,"net: active driver is %s\n", drv->name);
            return;
        }
        drv = drv->next;
    }
    printk(T,"net: no usable driver found\n");
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
