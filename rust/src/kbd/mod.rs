#![no_std]

pub mod keymap;

use crate::kbd::keymap::{hid_mod_to_ark_scancode, hid_to_ark_scancode};
use crate::ring::RingBuf;

static mut RING: RingBuf = RingBuf::new();
static mut PREV_REPORT: [u8; 8] = [0; 8];
static mut KBD_PRESENT: bool = false;

const USB_KBD_RINGBUF_SZ: usize = 64;

fn ring_push(sc: u8) {
    unsafe {
        let next = (RING.tail + 1) & (USB_KBD_RINGBUF_SZ - 1);
        if next != RING.head {
            RING.buf[RING.tail] = sc;
            RING.tail = next;
        }
    }
}

fn ring_pop() -> u8 {
    unsafe {
        if RING.head == RING.tail {
            return 0;
        }
        let v = RING.buf[RING.head];
        RING.head = (RING.head + 1) & (USB_KBD_RINGBUF_SZ - 1);
        v
    }
}

fn decode_report(report: &[u8; 8]) {
    unsafe {
        let mod_old = PREV_REPORT[0];
        let mod_new = report[0];
        let mod_changed = mod_old ^ mod_new;

        let mut bit = 0u8;
        while bit < 8 {
            if (mod_changed & (1 << bit)) != 0 {
                if let Some(sc) = hid_mod_to_ark_scancode(bit) {
                    if (mod_new & (1 << bit)) != 0 {
                        ring_push(sc);
                    } else {
                        ring_push(sc | 0x80);
                    }
                }
            }
            bit += 1;
        }

        let mut i = 2usize;
        while i < 8 {
            let kc = PREV_REPORT[i];
            if kc != 0 && kc != 1 {
                let mut still = false;
                let mut j = 2usize;
                while j < 8 {
                    if report[j] == kc {
                        still = true;
                        break;
                    }
                    j += 1;
                }
                if !still {
                    if let Some(sc) = hid_to_ark_scancode(kc) {
                        ring_push(sc | 0x80);
                    }
                }
            }
            i += 1;
        }

        i = 2;
        while i < 8 {
            let kc = report[i];
            if kc != 0 && kc != 1 {
                let mut was = false;
                let mut j = 2usize;
                while j < 8 {
                    if PREV_REPORT[j] == kc {
                        was = true;
                        break;
                    }
                    j += 1;
                }
                if !was {
                    if let Some(sc) = hid_to_ark_scancode(kc) {
                        ring_push(sc);
                    }
                }
            }
            i += 1;
        }

        PREV_REPORT = *report;
    }
}

#[no_mangle]
pub extern "C" fn rust_usb_kbd_has_input() -> bool {
    unsafe { RING.has_data() }
}

#[no_mangle]
pub extern "C" fn rust_usb_kbd_getc() -> u8 {
    ring_pop()
}

#[no_mangle]
pub extern "C" fn rust_usb_kbd_present() -> bool {
    unsafe { KBD_PRESENT }
}

#[no_mangle]
pub extern "C" fn rust_usb_kbd_set_present(present: bool) {
    unsafe {
        KBD_PRESENT = present;
    }
}

#[no_mangle]
pub extern "C" fn rust_usb_kbd_decode_report(report: *const u8) {
    unsafe {
        let r = &*(report as *const [u8; 8]);
        decode_report(r);
    }
}

pub fn init() -> i32 {
    0
}
