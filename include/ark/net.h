#pragma once

#include "ark/types.h"

/**
 * Network driver interface; supports basic packet send/receive and MAC
 * acquisition.  New NIC drivers should populate and register one of these
 * instances during their init routine.
 */

typedef struct net_driver {
    const char *name;
    int (*probe)(void);                      /* return 0 if device active */
    int (*init)(void);                       /* prepare hardware */
    int (*send)(const void *buf, u32 len);   /* send raw ethernet frame */
    int (*recv)(void *buf, u32 maxlen);      /* receive into buffer, return len */
    int (*get_mac)(u8 mac[6]);               /* fill MAC address, 0 on success */
    struct net_driver *next;
} net_driver_t;

/* driver registration (called by driver module) */
void net_register_driver(net_driver_t *drv);

/* global operations */
void net_init_all(void);
int  net_send(const void *buf, u32 len);
int  net_recv(void *buf, u32 maxlen);
int  net_get_mac(u8 mac[6]);

/* active driver (NULL if none) */
extern net_driver_t *g_net_driver;
