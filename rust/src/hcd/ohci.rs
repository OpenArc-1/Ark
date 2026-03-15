#![allow(dead_code)]

use crate::allocator::{alloc, phys_lo};
use crate::hcd::traits::UsbHostController;
use crate::usb_types::UsbSetupPacket;
use crate::x86_io::{mmio_read32 as mmrd, mmio_write32 as mmwr, msleep};

const OHCI_REVISION:  u32 = 0x00;
const OHCI_CONTROL:   u32 = 0x04;
const OHCI_CMDSTATUS: u32 = 0x08;
const OHCI_INTRSTATUS: u32 = 0x0C;
const OHCI_INTRENABLE: u32 = 0x10;
const OHCI_HCCA:      u32 = 0x18;
const OHCI_FMINTERVAL: u32 = 0x34;
const OHCI_PERIODICST: u32 = 0x40;
const OHCI_RHDESCRA:  u32 = 0x48;
const OHCI_RHSTATUS:  u32 = 0x50;
const OHCI_RHPORT0:   u32 = 0x54;

const OHCI_CTRL_HCFS_MASK: u32 = 0xC0;
const OHCI_CTRL_HCFS_OPER: u32 = 2 << 6;
const OHCI_CTRL_IR:  u32 = 1 << 8;
const OHCI_CS_HCR:   u32 = 1 << 0;
const OHCI_CS_OCR:   u32 = 1 << 3;

const OHCI_PORT_CCS:  u32 = 1 << 0;
const OHCI_PORT_PES:  u32 = 1 << 1;
const OHCI_PORT_PRS:  u32 = 1 << 4;
const OHCI_PORT_LSDA: u32 = 1 << 9;
const OHCI_PORT_PRSC: u32 = 1 << 20;

#[repr(C, align(256))]
#[derive(Clone, Copy)]
struct OhciHcca {
    intr:     [u32; 32],
    frame_no: u16,
    pad:      u16,
    done_head: u32,
    reserved: [u8; 116],
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct OhciEd {
    ctrl:   u32,
    tailp:  u32,
    headp:  u32,
    nexted: u32,
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct OhciTd {
    ctrl:   u32,
    cbp:    u32,
    nexttd: u32,
    be:     u32,
}

const OHCI_ED_FA:   u32 = 0x7F;
const OHCI_ED_EN:   u32 = 0xF << 7;
const OHCI_ED_MPS:  u32 = 0x7FF << 16;
const OHCI_ED_LS:   u32 = 1 << 13;
const OHCI_ED_SKIP: u32 = 1 << 14;

const OHCI_TD_ROUNDING: u32 = 1 << 18;
const OHCI_TD_DP_IN:    u32 = 2 << 19;
const OHCI_TD_DP_OUT:   u32 = 1 << 19;
const OHCI_TD_DP_SETUP: u32 = 0;
const OHCI_TD_DI_NONE:  u32 = 7 << 21;
const OHCI_TD_DI_IMM:   u32 = 0;
const OHCI_TD_T_DATA0:  u32 = 2 << 24;
const OHCI_TD_T_DATA1:  u32 = 3 << 24;
const OHCI_TD_CC_MASK:  u32 = 0xF << 28;
const OHCI_TD_ACTIVE:   u32 = 0xF << 28;

pub struct OhciController {
    base:    usize,          // MMIO base (pointer-sized)
    hcca:    *mut OhciHcca,
    ed_ctrl: *mut OhciEd,
    ed_intr: *mut OhciEd,
}

impl OhciController {
    pub fn new(mmio_base: usize) -> Self {
        Self {
            base: mmio_base,
            hcca: core::ptr::null_mut(),
            ed_ctrl: core::ptr::null_mut(),
            ed_intr: core::ptr::null_mut(),
        }
    }

    fn wait_td(&self, td: &OhciTd) -> u32 {
        for _ in 0..500 {
            msleep(1);
            if (td.ctrl & OHCI_TD_ACTIVE) != OHCI_TD_ACTIVE {
                return (td.ctrl >> 28) & 0xF;
            }
        }
        0xF
    }
}

impl UsbHostController for OhciController {
    fn init(&mut self) -> bool {
        unsafe {
            // Release SMM ownership if needed
            if (mmrd(self.base, OHCI_CONTROL) & OHCI_CTRL_IR) != 0 {
                mmwr(self.base, OHCI_CMDSTATUS, OHCI_CS_OCR);
                for _ in 0..100 {
                    msleep(5);
                    if (mmrd(self.base, OHCI_CONTROL) & OHCI_CTRL_IR) == 0 {
                        break;
                    }
                }
            }

            let fmi = mmrd(self.base, OHCI_FMINTERVAL);
            let fmi = if (fmi & 0x3FFF) == 0 { 0xA7782EDFu32 } else { fmi };

            // Software reset
            mmwr(self.base, OHCI_CMDSTATUS, OHCI_CS_HCR);
            for _ in 0..30 {
                msleep(1);
                if (mmrd(self.base, OHCI_CMDSTATUS) & OHCI_CS_HCR) == 0 {
                    break;
                }
            }
            msleep(10);

            mmwr(self.base, OHCI_FMINTERVAL, fmi);
            mmwr(self.base, OHCI_PERIODICST, ((fmi & 0x3FFF) * 9) / 10);

            // Allocate HCCA (256-byte aligned)
            self.hcca = alloc(256) as *mut OhciHcca;
            if self.hcca.is_null() {
                return false;
            }
            core::ptr::write_bytes(self.hcca as *mut u8, 0, 256);
            for i in 0..32 {
                (*self.hcca).intr[i] = 1; // terminate interrupt list
            }
            // OHCI HCCA register is 32-bit; OHCI is always ≤4 GB physical
            mmwr(self.base, OHCI_HCCA, phys_lo(self.hcca as *const u8));

            // Clear interrupts & enable
            mmwr(self.base, 0x14, 0xFFFFFFFF); // HcInterruptEnable
            mmwr(self.base, 0x0C, 0xFFFFFFFF); // HcInterruptStatus

            // Go operational, enable control & bulk list processing
            let ctrl = mmrd(self.base, OHCI_CONTROL);
            let ctrl = (ctrl & !OHCI_CTRL_HCFS_MASK) | OHCI_CTRL_HCFS_OPER | (1 << 2);
            mmwr(self.base, OHCI_CONTROL, ctrl);
            msleep(5);

            self.ed_ctrl = alloc(16) as *mut OhciEd;
            self.ed_intr = alloc(16) as *mut OhciEd;

            !self.ed_ctrl.is_null() && !self.ed_intr.is_null()
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
            let tds = alloc(48) as *mut OhciTd;
            if tds.is_null() {
                return -1;
            }

            // TD 0: SETUP
            (*tds.add(0)).ctrl = OHCI_TD_ACTIVE | OHCI_TD_DP_SETUP | OHCI_TD_DI_NONE | OHCI_TD_T_DATA0;
            (*tds.add(0)).cbp    = phys_lo(setup as *const _ as *const u8);
            (*tds.add(0)).nexttd = phys_lo(tds.add(1) as *const u8);
            (*tds.add(0)).be     = phys_lo(setup as *const _ as *const u8) + 7;

            // TD 1: DATA (or dummy skip)
            if !data.is_empty() {
                let dp = if is_in { OHCI_TD_DP_IN } else { OHCI_TD_DP_OUT };
                (*tds.add(1)).ctrl = OHCI_TD_ACTIVE | dp | OHCI_TD_ROUNDING | OHCI_TD_DI_NONE | OHCI_TD_T_DATA1;
                (*tds.add(1)).cbp    = phys_lo(data.as_mut_ptr());
                (*tds.add(1)).nexttd = phys_lo(tds.add(2) as *const u8);
                (*tds.add(1)).be     = phys_lo(data.as_mut_ptr()) + (data.len() - 1) as u32;
            } else {
                (*tds.add(1)).ctrl   = 0;
                (*tds.add(1)).cbp    = 0;
                (*tds.add(1)).nexttd = phys_lo(tds.add(2) as *const u8);
                (*tds.add(1)).be     = 0;
            }

            // TD 2: STATUS (direction opposite to data)
            let dp = if is_in { OHCI_TD_DP_OUT } else { OHCI_TD_DP_IN };
            (*tds.add(2)).ctrl   = OHCI_TD_ACTIVE | dp | OHCI_TD_DI_IMM | OHCI_TD_T_DATA1;
            (*tds.add(2)).cbp    = 0;
            (*tds.add(2)).nexttd = phys_lo(tds as *const u8) | 1; // null terminator
            (*tds.add(2)).be     = 0;

            // Set up ED
            let mps = 8u32;
            let mut ed_ctrl_val = (addr as u32 & OHCI_ED_FA)
                | ((ep as u32 & 0xF) << 7)
                | ((mps & 0x7FF) << 16);
            if is_ls { ed_ctrl_val |= OHCI_ED_LS; }

            (*self.ed_ctrl).ctrl   = ed_ctrl_val;
            (*self.ed_ctrl).tailp  = phys_lo(tds as *const u8) | 1; // halted/null
            (*self.ed_ctrl).headp  = phys_lo(tds as *const u8);
            (*self.ed_ctrl).nexted = 0;

            // Point HcControlHeadED at our ED and kick CLF
            mmwr(self.base, 0x20, phys_lo(self.ed_ctrl as *const u8)); // HcControlHeadED
            mmwr(self.base, OHCI_CMDSTATUS, 1 << 1);                    // CLF

            // Enable control list processing
            let ctrl = mmrd(self.base, OHCI_CONTROL);
            mmwr(self.base, OHCI_CONTROL, ctrl | (1 << 4));

            let cc = self.wait_td(&*tds.add(2));

            // Disable control list processing
            let ctrl = mmrd(self.base, OHCI_CONTROL);
            mmwr(self.base, OHCI_CONTROL, ctrl & !(1 << 4));

            if cc != 0 { return -1; }
            if !data.is_empty() { data.len() as i32 } else { 0 }
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
            let td = alloc(16) as *mut OhciTd;
            if td.is_null() { return -1; }

            let tog = OHCI_TD_T_DATA0;
            (*td).ctrl   = OHCI_TD_ACTIVE | OHCI_TD_DP_IN | OHCI_TD_ROUNDING | OHCI_TD_DI_IMM | tog;
            (*td).cbp    = phys_lo(buf.as_mut_ptr());
            (*td).nexttd = 0;
            (*td).be     = phys_lo(buf.as_mut_ptr()) + (buf.len() - 1) as u32;

            let mps = buf.len() as u32;
            let mut ec = (addr as u32 & OHCI_ED_FA)
                | ((ep as u32 & 0xF) << 7)
                | ((mps & 0x7FF) << 16);
            if is_ls { ec |= OHCI_ED_LS; }

            (*self.ed_intr).ctrl   = ec;
            (*self.ed_intr).tailp  = phys_lo(td as *const u8) | 1;
            (*self.ed_intr).headp  = phys_lo(td as *const u8);
            (*self.ed_intr).nexted = 0;

            // HCCA register holds the physical address we wrote at init time
            let hcca_phys32 = mmrd(self.base, OHCI_HCCA);
            // Reconstruct pointer from the 32-bit physical address stored in the register.
            // On a bare-metal kernel physical == virtual so this is correct on 32-bit.
            // On 64-bit the heap is still in low memory so the cast is safe.
            let hcca = hcca_phys32 as usize as *mut OhciHcca;
            for i in 0..32 {
                (*hcca).intr[i] = phys_lo(self.ed_intr as *const u8);
            }

            // Enable periodic list processing
            let ctrl = mmrd(self.base, OHCI_CONTROL);
            mmwr(self.base, OHCI_CONTROL, ctrl | (1 << 2));

            let cc = self.wait_td(&*td);

            // Disable periodic list
            let ctrl = mmrd(self.base, OHCI_CONTROL);
            mmwr(self.base, OHCI_CONTROL, ctrl & !(1 << 2));

            // Restore idle interrupt list
            for i in 0..32 {
                (*hcca).intr[i] = 1;
            }

            // cc == 0: success; cc == 0xF: still active (timeout treated as data)
            if cc == 0 || cc == 0xF {
                *toggle ^= 1;
                buf.len() as i32
            } else {
                -1
            }
        }
    }

    fn get_port_status(&self, port: u8) -> u32 {
        mmrd(self.base, OHCI_RHPORT0 + (port as u32) * 4)
    }

    fn reset_port(&mut self, port: u8) -> bool {
        let port_reg = OHCI_RHPORT0 + (port as u32) * 4;
        mmwr(self.base, port_reg, OHCI_PORT_PRS);
        for _ in 0..100 {
            msleep(1);
            if (mmrd(self.base, port_reg) & OHCI_PORT_PRS) == 0 {
                break;
            }
        }
        mmwr(self.base, port_reg, OHCI_PORT_PRSC);
        msleep(10);
        (mmrd(self.base, port_reg) & OHCI_PORT_PES) != 0
    }
}
