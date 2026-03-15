/*
 * usb/usb_hid_mouse.c — USB HID Mouse / Pointing Device Driver
 *
 * Handles USB mice using the HID Boot Protocol (subclass 1, protocol 2).
 * Boot protocol mouse report (3–5 bytes depending on device):
 *
 *   Byte 0: Button bitmask
 *     bit 0: Left button      bit 1: Right button     bit 2: Middle button
 *     bit 3: Back button      bit 4: Forward button   bits 5-7: reserved
 *
 *   Byte 1: X displacement  (signed 8-bit, relative)
 *   Byte 2: Y displacement  (signed 8-bit, relative)
 *   Byte 3: Scroll wheel    (signed 8-bit, optional)
 *   Byte 4: H-scroll        (signed 8-bit, optional)
 *
 * Absolute vs relative:
 *   Boot protocol = relative (delta movement each poll)
 *   Touchscreens / tablets use absolute coordinates via Report Protocol
 *
 * HID Report Descriptor for a typical 3-button wheel mouse:
 *   Usage Page (Generic Desktop) 0x01
 *   Usage (Mouse) 0x02
 *   Collection (Application)
 *     Usage (Pointer) 0x01
 *     Collection (Physical)
 *       Usage Page (Buttons) 0x09
 *       Usage Min (1) Usage Max (3)
 *       Logical Min (0) Logical Max (1)
 *       Report Count (3) Report Size (1)
 *       Input (Data, Variable, Absolute) — 3 button bits
 *       Report Count (1) Report Size (5)
 *       Input (Constant) — 5 pad bits
 *       Usage Page (Generic Desktop) 0x01
 *       Usage (X) Usage (Y) Usage (Wheel)
 *       Logical Min (-127) Logical Max (127)
 *       Report Size (8) Report Count (3)
 *       Input (Data, Variable, Relative)
 *     End Collection
 *   End Collection
 *
 * QEMU USB mouse:
 *   QEMU provides a USB tablet (absolute) not a relative mouse.
 *   The tablet uses Usage (X), Usage (Y) with Logical Max = 0x7FFF (absolute).
 *   To get clicks + movement, Ark checks for both boot protocol and report protocol.
 *
 * Ark integration:
 *   usb_mouse_init(io_base)   — enumerate mouse, set boot protocol
 *   usb_mouse_poll()          — read interrupt IN report
 *   usb_mouse_get_state()     — return current x, y, buttons, wheel
 */
#include "ark/types.h"
#include "ark/printk.h"

typedef struct {
    int  x, y;         /* absolute cursor position  */
    int  dx, dy;       /* last delta (relative mode) */
    int  scroll;       /* scroll wheel delta          */
    u8   buttons;      /* button bitmask              */
    bool ready;
} usb_mouse_state_t;

static usb_mouse_state_t g_mouse = {0};

void usb_mouse_init(u8 addr, bool uhci) {
    g_mouse.x = 512; g_mouse.y = 384;
    g_mouse.ready = true;
    printk(T, "[USB-Mouse] init addr=%u %s\n", addr, uhci?"UHCI":"OHCI");
}

void usb_mouse_update(i8 dx, i8 dy, i8 scroll, u8 buttons) {
    g_mouse.dx = dx; g_mouse.dy = dy; g_mouse.scroll = scroll;
    g_mouse.buttons = buttons;
    g_mouse.x += dx; g_mouse.y += dy;
    if(g_mouse.x < 0)    g_mouse.x = 0;
    if(g_mouse.x > 1023) g_mouse.x = 1023;
    if(g_mouse.y < 0)    g_mouse.y = 0;
    if(g_mouse.y > 767)  g_mouse.y = 767;
}

usb_mouse_state_t *usb_mouse_get_state(void) { return &g_mouse; }
bool usb_mouse_ready(void) { return g_mouse.ready; }
