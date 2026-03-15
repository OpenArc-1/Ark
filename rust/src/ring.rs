#![no_std]

pub const USB_KBD_RINGBUF_SZ: usize = 64;

pub struct RingBuf {
    pub buf: [u8; USB_KBD_RINGBUF_SZ],
    pub head: usize,
    pub tail: usize,
}

impl RingBuf {
    pub const fn new() -> Self {
        Self {
            buf: [0; USB_KBD_RINGBUF_SZ],
            head: 0,
            tail: 0,
        }
    }

    pub fn push(&mut self, value: u8) -> bool {
        let next = (self.tail + 1) & (USB_KBD_RINGBUF_SZ - 1);
        if next == self.head {
            return false;
        }
        self.buf[self.tail] = value;
        self.tail = next;
        true
    }

    pub fn pop(&mut self) -> u8 {
        if self.head == self.tail {
            return 0;
        }
        let value = self.buf[self.head];
        self.head = (self.head + 1) & (USB_KBD_RINGBUF_SZ - 1);
        value
    }

    pub fn is_empty(&self) -> bool {
        self.head == self.tail
    }

    pub fn has_data(&self) -> bool {
        self.head != self.tail
    }
}

impl Default for RingBuf {
    fn default() -> Self {
        Self::new()
    }
}
