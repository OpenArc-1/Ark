#![allow(dead_code)]

use crate::allocator::{alloc, phys_lo};
use crate::hcd::traits::UsbHostController;
use crate::usb_types::{UsbSetupPacket, USB_PID_IN, USB_PID_OUT, USB_PID_SETUP};
use crate::x86_io::{inb, inl, inw, msleep, outb, outl, outw};

const UHCI_CMD:    u16 = 0x00;
const UHCI_STS:    u16 = 0x02;
const UHCI_INTR:   u16 = 0x04;
const UHCI_FRNUM:  u16 = 0x06;
const UHCI_FLBASE: u16 = 0x08;
const UHCI_SOF:    u16 = 0x0C;
const UHCI_PORT1:  u16 = 0x10;
const UHCI_PORT2:  u16 = 0x12;

const UHCI_CMD_RS:   u16 = 1 << 0;
const UHCI_CMD_HCRST: u16 = 1 << 1;
const UHCI_CMD_GRST: u16 = 1 << 2;
const UHCI_CMD_CF:   u16 = 1 << 6;
const UHCI_CMD_MAXP: u16 = 1 << 7;

const UHCI_PORT_CCS:  u16 = 1 << 0;
const UHCI_PORT_CSC:  u16 = 1 << 1;
const UHCI_PORT_PED:  u16 = 1 << 2;
const UHCI_PORT_LSDA: u16 = 1 << 8;
const UHCI_PORT_RST:  u16 = 1 << 9;

const UHCI_TD_TERMINATE: u32 = 1;
const UHCI_TD_QH:    u32 = 1 << 1;
const UHCI_TD_DEPTH: u32 = 1 << 2;

const UHCI_TD_ACTIVE:  u32 = 1 << 23;
const UHCI_TD_IOC:     u32 = 1 << 24;
const UHCI_TD_LS:      u32 = 1 << 26;
const UHCI_TD_ERRMASK: u32 = 3 << 27;
const UHCI_TD_ERR3:    u32 = 3 << 27;
const UHCI_TD_STALL:   u32 = 1 << 22;
const UHCI_TD_SPD:     u32 = 1 << 29;

/// UHCI Transfer Descriptor.
/// All pointer fields are 32-bit physical addresses — UHCI is a PCI 2.x bus-master
/// device and only supports 32-bit DMA, so phys_lo() is correct here regardless
/// of whether we're running a 32- or 64-bit kernel (as long as the heap is in
/// the low 4 GB, which our static allocator guarantees).
#[repr(C, align(16))]
#[derive(Clone, Copy)]
struct UhciTd {
    link: u32,
    ctrl: u32,
    token: u32,
    buf: u32,
}

#[repr(C, align(16))]
#[derive(Clone, Copy)]
struct UhciQh {
    hlink: u32,
    elink: u32,
}

/// Build a UHCI TD token word.
fn td_token(pid: u8, addr: u8, ep: u8, tog: u8, len: u32) -> u32 {
    (pid as u32)
        | ((addr as u32) << 8)
        | ((ep as u32) << 15)
        | ((tog as u32) << 19)
        | ((len - 1) << 21)
}

pub struct UhciController {
    base: u16,              // I/O port base (always 16-bit for UHCI)
    frame_list: *mut u32,   // 1024-entry frame list; entries are 32-bit physical addresses
}

impl UhciController {
    pub fn new(io_base: u16) -> Self {
        Self {
            base: io_base,
            frame_list: core::ptr::null_mut(),
        }
    }

    fn run_td(&mut self, td: &mut UhciTd) -> i32 {
        // Put the single TD in frame 0 and wait
        unsafe {
            *self.frame_list = phys_lo(td as *const _ as *const u8);
        }
        for _ in 0..500 {
            msleep(1);
            if (td.ctrl & UHCI_TD_ACTIVE) == 0 {
                unsafe { *self.frame_list = UHCI_TD_TERMINATE; }
                if (td.ctrl & UHCI_TD_STALL) != 0 {
                    return -1;
                }
                return ((td.ctrl & 0x7FF) + 1) as i32;
            }
        }
        unsafe { *self.frame_list = UHCI_TD_TERMINATE; }
        -1
    }
}

impl UsbHostController for UhciController {
    fn init(&mut self) -> bool {
        unsafe {
            outw(self.base + UHCI_CMD, UHCI_CMD_HCRST);
            for _ in 0..100 {
                msleep(1);
                if (inw(self.base + UHCI_CMD) & UHCI_CMD_HCRST) == 0 {
                    break;
                }
            }
            if (inw(self.base + UHCI_CMD) & UHCI_CMD_HCRST) != 0 {
                return false;
            }

            outw(self.base + UHCI_STS,  0x3F);
            outw(self.base + UHCI_INTR, 0);
            outw(self.base + UHCI_FRNUM, 0);

            let fl_size = 1024_usize * 4;
            self.frame_list = alloc(fl_size) as *mut u32;
            if self.frame_list.is_null() {
                return false;
            }
            for i in 0..1024 {
                *self.frame_list.add(i) = UHCI_TD_TERMINATE;
            }

            // FLBASE is a 32-bit register; our heap is in static memory (low 4 GB)
            // so phys_lo is always correct here.
            outl(self.base + UHCI_FLBASE, phys_lo(self.frame_list as *const u8));
            outb(self.base + UHCI_SOF, 0x40);
            outw(self.base + UHCI_CMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
            msleep(5);

            (inw(self.base + UHCI_STS) & (1 << 5)) == 0
        }
    }

    fn control_transfer(
        &mut self,
        addr: u8,
        ep: u8,
        setup: &UsbSetupPacket,
        data: &mut [u8],
        is_in: bool,
        is_ls: bool,
    ) -> i32 {
        unsafe {
            // Allocate 3 TDs (SETUP + DATA + STATUS)
            let tds = alloc(3 * 16) as *mut UhciTd;
            if tds.is_null() { return -1; }

            let ls_bit = if is_ls { UHCI_TD_LS } else { 0 };

            // TD 0: SETUP
            (*tds.add(0)).link  = phys_lo(tds.add(1) as *const u8) | UHCI_TD_DEPTH;
            (*tds.add(0)).ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3;
            (*tds.add(0)).token = td_token(USB_PID_SETUP, addr, ep, 0, 8);
            (*tds.add(0)).buf   = phys_lo(setup as *const _ as *const u8);

            // TD 1: DATA (or empty filler)
            let data_result: i32;
            if !data.is_empty() {
                let pid = if is_in { USB_PID_IN } else { USB_PID_OUT };
                (*tds.add(1)).link  = phys_lo(tds.add(2) as *const u8) | UHCI_TD_DEPTH;
                (*tds.add(1)).ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3 | UHCI_TD_SPD;
                (*tds.add(1)).token = td_token(pid, addr, ep, 1, data.len() as u32);
                (*tds.add(1)).buf   = phys_lo(data.as_mut_ptr());
                data_result = data.len() as i32;
            } else {
                (*tds.add(1)).link  = phys_lo(tds.add(2) as *const u8) | UHCI_TD_DEPTH;
                (*tds.add(1)).ctrl  = 0;
                (*tds.add(1)).token = 0;
                (*tds.add(1)).buf   = 0;
                data_result = 0;
            }

            // TD 2: STATUS (direction opposite to data phase)
            let stat_pid = if is_in { USB_PID_OUT } else { USB_PID_IN };
            (*tds.add(2)).link  = UHCI_TD_TERMINATE;
            (*tds.add(2)).ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3 | UHCI_TD_IOC;
            (*tds.add(2)).token = td_token(stat_pid, addr, ep, 1, 1);
            (*tds.add(2)).buf   = 0;

            // Schedule the chain
            *self.frame_list = phys_lo(tds as *const u8);

            for _ in 0..500 {
                msleep(1);
                if ((*tds.add(2)).ctrl & UHCI_TD_ACTIVE) == 0 {
                    *self.frame_list = UHCI_TD_TERMINATE;
                    if ((*tds.add(2)).ctrl & UHCI_TD_STALL) != 0 {
                        return -1;
                    }
                    return data_result;
                }
            }
            *self.frame_list = UHCI_TD_TERMINATE;
            -1
        }
    }

    fn interrupt_in(
        &mut self,
        addr: u8,
        ep: u8,
        buf: &mut [u8],
        toggle: &mut u8,
        is_ls: bool,
    ) -> i32 {
        unsafe {
            let td = alloc(16) as *mut UhciTd;
            if td.is_null() { return -1; }

            let ls_bit = if is_ls { UHCI_TD_LS } else { 0 };
            (*td).link  = UHCI_TD_TERMINATE;
            (*td).ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3 | UHCI_TD_IOC | UHCI_TD_SPD;
            (*td).token = td_token(USB_PID_IN, addr, ep, *toggle, buf.len() as u32);
            (*td).buf   = phys_lo(buf.as_mut_ptr());

            let r = self.run_td(&mut *td);
            if r >= 0 {
                *toggle ^= 1;
            }
            r
        }
    }

    fn get_port_status(&self, port: u8) -> u32 {
        let port_reg = if port == 0 { UHCI_PORT1 } else { UHCI_PORT2 };
        inw(self.base + port_reg) as u32
    }

    fn reset_port(&mut self, port: u8) -> bool {
        let port_reg = if port == 0 { UHCI_PORT1 } else { UHCI_PORT2 };
        outw(self.base + port_reg, UHCI_PORT_RST);
        msleep(50);
        outw(self.base + port_reg, 0);
        msleep(10);
        let status = inw(self.base + port_reg);
        (status & UHCI_PORT_PED) != 0
    }
}
