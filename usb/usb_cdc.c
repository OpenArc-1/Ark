/*
 * usb/usb_cdc.c — USB Communications Device Class (CDC)
 *
 * CDC provides virtual serial ports, Ethernet adapters, and modems over USB.
 * Class code: 0x02 (on the device/IAD descriptor)
 *
 * Subclasses used in Ark:
 *   0x02  Abstract Control Model (ACM) — virtual serial / UART
 *         Used by: Arduino, STM32 USB UART, CP2102, CH340, FTDI
 *   0x06  Ethernet Networking Control Model (ECM) — USB Ethernet
 *         Used by: Raspberry Pi USB gadget, USB-C Ethernet dongles
 *   0x0A  Network Control Model (NCM) — high-performance USB Ethernet
 *         Used by: USB 3.x docks, Thunderbolt adapters
 *
 * ACM interface pair:
 *   Interface 0 (CDC control, class 0x02 subclass 0x02):
 *     Endpoint 0:  Control (default pipe)
 *     Endpoint N:  Interrupt IN — serial state notifications
 *   Interface 1 (CDC data, class 0x0A):
 *     Endpoint M:  Bulk OUT — host → device (TX)
 *     Endpoint K:  Bulk IN  — device → host (RX)
 *
 * ACM class requests (bmRequestType = 0x21):
 *   SET_LINE_CODING    0x20 — baud, stop bits, parity, data bits
 *   GET_LINE_CODING    0x21 — read current line settings
 *   SET_CONTROL_LINE_STATE 0x22 — DTR (bit 0), RTS (bit 1)
 *   SEND_BREAK         0x23 — send RS-232 break signal
 *
 * Line Coding structure (7 bytes):
 *   u32 dwDTERate    — baud rate (e.g. 115200)
 *   u8  bCharFormat  — stop bits: 0=1, 1=1.5, 2=2
 *   u8  bParityType  — 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
 *   u8  bDataBits    — 5, 6, 7, 8, or 16
 *
 * ECM/NCM (USB Ethernet):
 *   Uses a control interface + data interface pair.
 *   Data interface has one Bulk IN and one Bulk OUT endpoint.
 *   Frames are raw Ethernet frames (14-byte header + payload).
 *   NCM adds a datagram pointer table for better throughput.
 *
 * Ark integration:
 *   cdc_acm_init(dev)      — set line coding, enable DTR/RTS
 *   cdc_acm_write(buf,len) — send bytes over Bulk OUT
 *   cdc_acm_read(buf,max)  — receive bytes from Bulk IN
 *   cdc_ecm_init(dev)      — init USB Ethernet interface
 *   cdc_ecm_send(frame)    — transmit Ethernet frame
 *   cdc_ecm_poll()         — check for received frames
 */
#include "ark/types.h"
#include "ark/printk.h"

typedef struct __attribute__((packed)) {
    u32 dwDTERate;
    u8  bCharFormat;
    u8  bParityType;
    u8  bDataBits;
} cdc_line_coding_t;

static cdc_line_coding_t g_line = { 115200, 0, 0, 8 };

void cdc_acm_init(u8 addr) {
    printk(T, "[CDC-ACM] init addr=%u baud=%u\n", addr, g_line.dwDTERate);
}

void cdc_ecm_init(u8 addr) {
    printk(T, "[CDC-ECM] init addr=%u\n", addr);
}
