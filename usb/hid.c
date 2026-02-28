/*
 * usb/hid.c — USB Human Interface Device (HID) class driver
 *
 * The USB HID class (bInterfaceClass=0x03) covers keyboards, mice,
 * gamepads, joysticks, touchscreens, and many other input devices.
 *
 * Two sub-protocols relevant to keyboards:
 *   Boot Protocol (bInterfaceSubClass=1, bInterfaceProtocol=1)
 *     Produces 8-byte reports without parsing a Report Descriptor.
 *     Structure: [modifiers, reserved, keycode×6]
 *     Supported by every BIOS and every HID keyboard.
 *     This is what usb_kbd.c uses.
 *
 *   Report Protocol (default)
 *     Device sends a Report Descriptor describing variable-length reports.
 *     Must send SET_PROTOCOL(Boot) to switch to Boot Protocol.
 *
 * HID class requests (bmRequestType = 0x21, target = interface):
 *   GET_REPORT    0x01  — read current report from device
 *   GET_IDLE      0x02  — read current idle rate
 *   GET_PROTOCOL  0x03  — 0=Boot, 1=Report
 *   SET_REPORT    0x09  — write report to device (LEDs, force feedback)
 *   SET_IDLE      0x0A  — set polling interval (0=only on change)
 *   SET_PROTOCOL  0x0B  — 0=Boot, 1=Report
 *
 * Report Descriptor parsing (Report Protocol only):
 *   Short items: 1-4 bytes. Byte 0: bTag(7:4) bType(3:2) bSize(1:0)
 *   bType: 0=Main 1=Global 2=Local 3=Reserved
 *   Main tags:  8=Input 9=Output A=Collection B=Feature C=End Collection
 *   Global tags: 0=Usage Page 1=Logical Min 2=Logical Max 7=Report Size
 *                8=Report Count 9=Report ID
 *   Local tags:  0=Usage 1=Usage Min 2=Usage Max
 *
 * Usage Pages relevant to keyboards:
 *   0x01  Generic Desktop  (Usage: Keyboard=0x06, Mouse=0x02)
 *   0x07  Keyboard/Keypad  (Usage: individual keycodes)
 *   0x08  LEDs             (Usage: Num Lock=1, Caps Lock=2, Scroll Lock=3)
 *   0x09  Button           (Usage: button 1, 2, 3...)
 *
 * HID Boot Protocol keyboard report (8 bytes):
 *   Byte 0: Modifier keys bitfield
 *     bit 0: Left Ctrl      bit 4: Right Ctrl
 *     bit 1: Left Shift     bit 5: Right Shift
 *     bit 2: Left Alt       bit 6: Right Alt
 *     bit 3: Left GUI/Win   bit 7: Right GUI/Win
 *   Byte 1: Reserved (always 0)
 *   Bytes 2-7: Up to 6 simultaneous keycodes (USB HID keycode values)
 *              0x00 = no key, 0x01 = Error Roll Over (too many keys)
 *
 * USB HID Keycode to ASCII mapping (boot protocol, page 0x07):
 *   0x04-0x1D: a-z          0x1E-0x27: 1-0
 *   0x28: Enter             0x29: Escape
 *   0x2A: Backspace         0x2B: Tab
 *   0x2C: Space             0x2D-0x38: punctuation
 *   0x39: Caps Lock         0x3A-0x45: F1-F12
 *   0x46: Print Screen      0x47: Scroll Lock
 *   0x48: Pause             0x49: Insert
 *   0x4A: Home              0x4B: Page Up
 *   0x4C: Delete            0x4D: End
 *   0x4E: Page Down         0x4F-0x52: arrows
 *   0x53: Num Lock          0x54-0x63: numpad
 *
 * LED output report (3 bits, 5 padding):
 *   bit 0: Num Lock         bit 1: Caps Lock       bit 2: Scroll Lock
 *
 * Ark integration:
 *   hid_set_leds(num, caps, scroll)    — send LED report to keyboard
 *   hid_parse_report(buf, len, out)    — decode boot protocol report
 *   hid_keycode_to_ascii(kc, shift)    — convert HID keycode to ASCII
 */
#include "ark/types.h"
#include "ark/printk.h"

/* ── HID report structure (boot protocol) ─────────────────── */
typedef struct __attribute__((packed)) {
    u8 modifiers;   /* see modifier bitfield above */
    u8 reserved;
    u8 keycode[6];  /* up to 6 simultaneous keys   */
} hid_boot_report_t;

/* ── Modifier bit masks ────────────────────────────────────── */
#define HID_MOD_LCTRL   (1u<<0)
#define HID_MOD_LSHIFT  (1u<<1)
#define HID_MOD_LALT    (1u<<2)
#define HID_MOD_LGUI    (1u<<3)
#define HID_MOD_RCTRL   (1u<<4)
#define HID_MOD_RSHIFT  (1u<<5)
#define HID_MOD_RALT    (1u<<6)
#define HID_MOD_RGUI    (1u<<7)
#define HID_MOD_SHIFT   (HID_MOD_LSHIFT|HID_MOD_RSHIFT)
#define HID_MOD_CTRL    (HID_MOD_LCTRL|HID_MOD_RCTRL)
#define HID_MOD_ALT     (HID_MOD_LALT|HID_MOD_RALT)

/* ── HID keycodes ──────────────────────────────────────────── */
#define HID_KEY_A        0x04
#define HID_KEY_Z        0x1D
#define HID_KEY_1        0x1E
#define HID_KEY_0        0x27
#define HID_KEY_ENTER    0x28
#define HID_KEY_ESC      0x29
#define HID_KEY_BSPACE   0x2A
#define HID_KEY_TAB      0x2B
#define HID_KEY_SPACE    0x2C
#define HID_KEY_CAPS     0x39
#define HID_KEY_F1       0x3A
#define HID_KEY_F12      0x45
#define HID_KEY_UP       0x52
#define HID_KEY_DOWN     0x51
#define HID_KEY_LEFT     0x50
#define HID_KEY_RIGHT    0x4F
#define HID_KEY_HOME     0x4A
#define HID_KEY_END      0x4D
#define HID_KEY_PGUP     0x4B
#define HID_KEY_PGDN     0x4E
#define HID_KEY_INSERT   0x49
#define HID_KEY_DELETE   0x4C

/* ── Keycode → ASCII tables ────────────────────────────────── */
/* Index = keycode - 0x04 (covers 0x04..0x63 = 96 entries) */
static const char hid_unshifted[96] = {
    /* 0x04-0x1D: a-z */
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    /* 0x1E-0x27: 1-0 */
    '1','2','3','4','5','6','7','8','9','0',
    /* 0x28-0x2C: Enter Esc BS Tab Space */
    '\r','\x1B','\b','\t',' ',
    /* 0x2D-0x38: - = [ ] \ # ; ' ` , . / */
    '-','=','[',']','\\','#',';','\'','`',',','.','/',
    /* 0x39-0x52: non-printable control keys (16 entries for Caps..F12 range) */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* CapsLk F1-F12 PrtSc ScrLk Pause */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,   /* Insert Home PgUp Del End PgDn Arrows NumLk */
    /* 0x53-0x63: numpad */
    '/',  '*',  '-',  '+',  '\r', '1','2','3','4','5',
    '6',  '7',  '8',  '9',  '0',  '.', '\\'
};

static const char hid_shifted[96] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    '\r','\x1B','\b','\t',' ',
    '_','+','{','}','|','~',':','"','~','<','>','?',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '/','*','-','+','\r','1','2','3','4','5',
    '6','7','8','9','0','.','|'
};

/* ── Public API ────────────────────────────────────────────── */
char hid_keycode_to_ascii(u8 keycode, bool shift) {
    if (keycode < 0x04 || keycode > 0x63) return 0;
    u8 idx = keycode - 0x04;
    return shift ? hid_shifted[idx] : hid_unshifted[idx];
}

bool hid_mod_shift(u8 mods) { return !!(mods & HID_MOD_SHIFT); }
bool hid_mod_ctrl(u8 mods)  { return !!(mods & HID_MOD_CTRL);  }
bool hid_mod_alt(u8 mods)   { return !!(mods & HID_MOD_ALT);   }

void hid_describe_report(const hid_boot_report_t *r) {
    if (!r) return;
    printk("[HID] mods=0x%02x keys:", (u32)r->modifiers);
    for (int i = 0; i < 6; i++) {
        if (r->keycode[i]) printk(" 0x%02x", (u32)r->keycode[i]);
    }
    printk("\n");
}
