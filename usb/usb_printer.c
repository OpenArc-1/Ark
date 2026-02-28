/*
 * usb/usb_printer.c — USB Printer Class
 *
 * USB printers use class 0x07, subclass 0x01, protocol 0x01/0x02/0x03.
 * Protocol: 1 = Unidirectional (bulk OUT only)
 *           2 = Bidirectional   (bulk OUT + bulk IN)
 *           3 = IEEE 1284.4 compatible bidirectional
 *
 * Class requests (bmRequestType = 0xA1):
 *   GET_DEVICE_ID  0x00 — IEEE 1284 device ID string (manufacturer, model, etc.)
 *   GET_PORT_STATUS 0x01 — port status byte
 *     bit 3: PAPER_EMPTY   bit 4: SELECT   bit 5: NOT_ERROR
 *   SOFT_RESET     0x02 — flush buffers, reset to power-on state
 *
 * Data transfer:
 *   Raw PCL, PostScript, or ZPL data sent via Bulk OUT endpoint.
 *   For bidirectional, status/response comes back via Bulk IN.
 *
 * Ark integration:
 *   usb_printer_init(dev)         — get device ID, check status
 *   usb_printer_write(data, len)  — send print data
 *   usb_printer_status()          — check paper/online/error state
 */
#include "ark/types.h"
#include "ark/printk.h"

void usb_printer_init(u8 addr) {
    printk(T, "[USB-Printer] init addr=%u\n", addr);
}
