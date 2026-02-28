/*
 * loopback.c - simple network driver that loops packets back to sender.
 * Useful for testing when no real NIC is present.
 */

#include "ark/net.h"
#include "ark/printk.h"
#include "ark/mem.h"

static int loop_probe(void) {
    /* always available */
    return 0;
}

static int loop_init(void) {
    printk(T,"loopback: driver initialized\n");
    return 0;
}

static int loop_send(const void *buf, u32 len) {
    /* simply copy into internal buffer and mark for recv() */
    if (len > 2048) return -1;
    static u8 saved[2048];
    memcpy(saved, buf, len);
    /* store length in first two bytes for simplicity */
    saved[0] = len & 0xFF;
    saved[1] = (len >> 8) & 0xFF;
    return 0;
}

static int loop_recv(void *buf, u32 maxlen) {
    static u8 saved[2048];
    u32 len = saved[0] | (saved[1] << 8);
    if (len == 0 || len > maxlen) return 0;
    memcpy(buf, saved, len);
    /* clear so we don't repeatedly send */
    saved[0] = saved[1] = 0;
    return len;
}

static int loop_get_mac(u8 mac[6]) {
    /* dummy MAC */
    mac[0]=0x02; mac[1]=0x00; mac[2]=0x00;
    mac[3]=0x00; mac[4]=0x00; mac[5]=0x01;
    return 0;
}

static net_driver_t loop_driver = {
    .name = "loopback",
    .probe = loop_probe,
    .init  = loop_init,
    .send  = loop_send,
    .recv  = loop_recv,
    .get_mac = loop_get_mac,
    .next = NULL,
};

static void __attribute__((constructor)) register_loop(void) {
    net_register_driver(&loop_driver);
}
