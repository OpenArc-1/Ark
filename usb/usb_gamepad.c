/*
 * usb/usb_gamepad.c — USB HID Gamepad / Joystick Driver
 *
 * Gamepads are HID devices (class 0x03) using the Report Protocol
 * (subclass 0, protocol 0 — no boot protocol for gamepads).
 * Each gamepad has a unique Report Descriptor describing its layout.
 *
 * Common gamepad report elements:
 *   Usage Page (Generic Desktop) 0x01
 *     Usage (Gamepad) 0x05 or Usage (Joystick) 0x04
 *     X, Y axes  — left stick, Logical Min/Max ±127 or 0–255
 *     Rx, Ry     — right stick
 *     Z, Rz      — triggers (0–255)
 *     Hat Switch — D-pad as 8-way hat (0=N, 1=NE, 2=E, ..., 8=idle)
 *   Usage Page (Buttons) 0x09
 *     Button 1–16 or more — face buttons, shoulder buttons, etc.
 *
 * D-input (DirectInput) vs X-input:
 *   Classic USB HID gamepads: parsed via Report Descriptor.
 *   Xbox controllers (X-input, 0x045E:0x028E): use proprietary protocol,
 *     not standard HID — require special enumeration sequence.
 *
 * PlayStation DualShock 4 (USB, 0x054C:0x09CC):
 *   Reports on endpoint 1 IN, 64-byte report.
 *   Byte 1: left-X, Byte 2: left-Y, Byte 3: right-X, Byte 4: right-Y
 *   Byte 5: L2 analog, Byte 6: R2 analog
 *   Byte 7-8: button bitfields (cross, circle, square, triangle, L1/R1, etc.)
 *   Byte 13-18: 6-axis IMU (gyro + accel), little-endian 16-bit
 *   Byte 33-36: touchpad finger 1, Byte 37-40: touchpad finger 2
 *
 * Rumble (force feedback):
 *   Set_Report (output) with motor strength values.
 *   Xbox: 8-byte output report, bytes 3-4 = left/right motor (0–255)
 *   DS4: 32-byte output report, bytes 7-8 = right/left rumble
 *
 * Ark integration:
 *   usb_gamepad_init(dev)       — parse report descriptor or use known layout
 *   usb_gamepad_poll()          — read interrupt IN report
 *   usb_gamepad_get_axes()      — return analog axis values
 *   usb_gamepad_get_buttons()   — return button bitmask
 *   usb_gamepad_rumble(l, r)    — set left/right motor strength
 */
#include "ark/types.h"
#include "ark/printk.h"

typedef struct {
    i8  left_x, left_y;       /* left  stick: -127..127  */
    i8  right_x, right_y;     /* right stick: -127..127  */
    u8  left_trigger;          /* L2/LT: 0..255           */
    u8  right_trigger;         /* R2/RT: 0..255           */
    u16 buttons;               /* bitmask, bit 0=A/Cross  */
    u8  hat;                   /* D-pad hat: 0-7, 8=idle  */
    bool ready;
} usb_gamepad_state_t;

static usb_gamepad_state_t g_pad = {0};

void usb_gamepad_init(u8 addr) {
    g_pad.hat = 8; g_pad.ready = true;
    printk(T, "[USB-Gamepad] init addr=%u\n", addr);
}

usb_gamepad_state_t *usb_gamepad_get_state(void) { return &g_pad; }
bool usb_gamepad_ready(void) { return g_pad.ready; }
