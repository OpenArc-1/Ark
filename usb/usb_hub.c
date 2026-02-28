/*
 * usb/usb_hub.c — USB Hub driver (USB 2.0, class 0x09)
 *
 * USB hubs allow one port to expand to multiple ports.
 * All USB root hubs are built into the host controller itself,
 * but external hubs are normal USB devices with class 0x09.
 *
 * Hub class requests (bmRequestType target = device or port):
 *   GET_STATUS       0x00  — hub or port status (4 bytes)
 *   CLEAR_FEATURE    0x01  — clear status bit
 *   SET_FEATURE      0x03  — set feature (power, reset)
 *   GET_DESCRIPTOR   0x06  — hub descriptor
 *   SET_DESCRIPTOR   0x07  — write hub descriptor
 *   CLEAR_TT_BUFFER  0x08  — transaction translator buffer clear
 *   RESET_TT         0x09  — reset transaction translator
 *   GET_TT_STATE     0x0A  — TT state
 *   STOP_TT          0x0B  — stop TT
 *
 * Hub Descriptor (wHubCharacteristics fields):
 *   Bits 1:0 — Logical Power Switching Mode
 *     00 = ganged, 01 = individual, 10/11 = reserved (compound device)
 *   Bit 2    — Compound device flag
 *   Bits 4:3 — Over-current Protection Mode
 *   Bits 6:5 — Transaction Translator Think Time (0=8 FS bit times)
 *   Bit 7    — Port Indicators Supported
 *   bNbrPorts — number of downstream ports
 *   DeviceRemovable — bitmask, bit N=1 means port N device is non-removable
 *
 * Port Status bits (wPortStatus in GET_STATUS response):
 *   Bit 0:  PORT_CONNECTION    — device present
 *   Bit 1:  PORT_ENABLE        — port enabled
 *   Bit 2:  PORT_SUSPEND       — port suspended
 *   Bit 3:  PORT_OVER_CURRENT  — overcurrent condition
 *   Bit 4:  PORT_RESET         — port reset in progress
 *   Bit 8:  PORT_POWER         — port power on
 *   Bit 9:  PORT_LOW_SPEED     — low-speed device attached
 *   Bit 10: PORT_HIGH_SPEED    — high-speed device attached
 *   Bit 11: PORT_TEST          — port in test mode
 *   Bit 12: PORT_INDICATOR     — port indicator on
 *
 * Port Change bits (wPortChange in GET_STATUS response):
 *   Bit 0:  C_PORT_CONNECTION  — connection status changed
 *   Bit 1:  C_PORT_ENABLE      — enable status changed
 *   Bit 2:  C_PORT_SUSPEND     — suspend status changed
 *   Bit 3:  C_PORT_OVER_CURRENT — overcurrent changed
 *   Bit 4:  C_PORT_RESET       — reset completed
 *
 * Hub Feature Selectors (for SET_FEATURE / CLEAR_FEATURE):
 *   PORT_CONNECTION=0 PORT_ENABLE=1 PORT_SUSPEND=2 PORT_OVER_CURRENT=3
 *   PORT_RESET=4      PORT_POWER=8  PORT_LOW_SPEED=9 C_PORT_CONNECTION=16
 *   C_PORT_ENABLE=17  C_PORT_SUSPEND=18 C_PORT_OVER_CURRENT=19
 *   C_PORT_RESET=20   PORT_TEST=21  PORT_INDICATOR=22
 *
 * Transaction Translator (TT):
 *   A TT is a speed-conversion engine built into high-speed hubs.
 *   It buffers full/low-speed transactions and presents them to the
 *   EHCI controller as if they were high-speed, allowing FS/LS devices
 *   to connect to EHCI ports via a hub without a companion controller.
 *
 * Ark integration:
 *   usb_hub_init(dev)     — enumerate hub, power up ports
 *   usb_hub_poll(dev)     — check interrupt IN for port changes
 *   usb_hub_reset_port(n) — issue port reset, wait for enable
 */
#include "ark/types.h"
#include "ark/printk.h"

/* ── Hub descriptor ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    u8  bDescLength;
    u8  bDescriptorType;   /* 0x29 = hub descriptor */
    u8  bNbrPorts;
    u16 wHubCharacteristics;
    u8  bPwrOn2PwrGood;    /* 2ms units between power-on and stable */
    u8  bHubContrCurrent;  /* max current for hub controller (mA) */
    u8  DeviceRemovable;   /* bit N = port N is non-removable */
    u8  PortPwrCtrlMask;   /* deprecated in USB 2.0, all 1s */
} usb_hub_desc_t;

/* ── Port status response ──────────────────────────────────── */
typedef struct __attribute__((packed)) {
    u16 wPortStatus;
    u16 wPortChange;
} usb_port_status_t;

#define PORT_CONNECTION  (1u<<0)
#define PORT_ENABLE      (1u<<1)
#define PORT_RESET       (1u<<4)
#define PORT_POWER       (1u<<8)
#define PORT_LOW_SPEED   (1u<<9)
#define PORT_HIGH_SPEED  (1u<<10)
#define C_PORT_CONN      (1u<<0)  /* in wPortChange */
#define C_PORT_RESET     (1u<<4)  /* in wPortChange */

/* ── Hub state ─────────────────────────────────────────────── */
#define USB_HUB_MAX_PORTS 8
typedef struct {
    u8  addr;          /* USB device address   */
    u8  num_ports;     /* downstream ports     */
    u8  port_status[USB_HUB_MAX_PORTS];
    bool powered;
} usb_hub_t;

static usb_hub_t g_hubs[4];
static u32       g_hub_count = 0;

void usb_hub_init(u8 addr, u8 num_ports) {
    if (g_hub_count >= 4) return;
    usb_hub_t *h = &g_hubs[g_hub_count++];
    h->addr = addr;
    h->num_ports = num_ports < USB_HUB_MAX_PORTS ? num_ports : USB_HUB_MAX_PORTS;
    h->powered = false;
    printk(T, "[HUB] hub at addr %u with %u ports\n", addr, h->num_ports);
}

void usb_hub_port_connected(u8 hub_addr, u8 port, bool low_speed) {
    printk(T, "[HUB] addr=%u port=%u: %s device connected\n",
           hub_addr, port, low_speed ? "LS" : "FS/HS");
}

u32 usb_hub_count(void) { return g_hub_count; }
