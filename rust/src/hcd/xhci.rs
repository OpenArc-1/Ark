#![allow(dead_code)]

use crate::allocator::{alloc, phys_lo, phys_hi};
use crate::hcd::traits::UsbHostController;
use crate::usb_types::UsbSetupPacket;
use crate::x86_io::{mmio_read32 as mmrd, mmio_write32 as mmwr, msleep};

// Capability register offsets (from cap base)
const XCAP_CAPLENGTH:  u32 = 0x00;
const XCAP_HCSPARAMS1: u32 = 0x04;
const XCAP_DBOFF:      u32 = 0x14;
const XCAP_RTSOFF:     u32 = 0x18;

// Operational register offsets (from op base)
const XOP_USBCMD: u32 = 0x00;
const XOP_USBSTS: u32 = 0x04;
const XOP_DNCTRL: u32 = 0x14;
const XOP_CRCR_LO: u32 = 0x18;   // 64-bit: lo word
const XOP_CRCR_HI: u32 = 0x1C;   // 64-bit: hi word
const XOP_DCBAAP_LO: u32 = 0x30; // 64-bit: lo word
const XOP_DCBAAP_HI: u32 = 0x34; // 64-bit: hi word
const XOP_CONFIG:  u32 = 0x38;
const XOP_PORTSC:  u32 = 0x400;

const XCMD_RUN:  u32 = 1 << 0;
const XCMD_HCRST: u32 = 1 << 1;
const XSTS_HCH:  u32 = 1 << 0;

const XPS_CCS:   u32 = 1 << 0;
const XPS_PED:   u32 = 1 << 1;
const XPS_PR:    u32 = 1 << 4;
const XPS_PP:    u32 = 1 << 9;
const XPS_SPEED: u32 = 0xF << 10;

const TRB_NORMAL:       u32 = 1;
const TRB_SETUP:        u32 = 2;
const TRB_DATA:         u32 = 3;
const TRB_STATUS:       u32 = 4;
const TRB_LINK:         u32 = 6;
const TRB_EVT_TRANSFER: u32 = 32;
const TRB_EVT_CMD_COMP: u32 = 33;
const TRB_EVT_PORT:     u32 = 34;
const TRB_CMD_ENABLE_SLOT: u32 = 9;
const TRB_CMD_ADDRESS_DEV: u32 = 11;

const TRB_C:   u32 = 1 << 0;
const TRB_TC:  u32 = 1 << 1;
const TRB_ENT: u32 = 1 << 1;
const TRB_ISP: u32 = 1 << 2;
const TRB_IOC: u32 = 1 << 5;
const TRB_IDT: u32 = 1 << 6;
const TRB_DIR_IN: u32 = 1 << 16;

const TRB_TYPE_SHIFT: u32 = 10;
const TRB_SLOT_SHIFT: u32 = 24;

/// A 16-byte xHCI TRB.
/// p0/p1 together form the 64-bit parameter field (lo / hi).
#[repr(C, align(16))]
#[derive(Clone, Copy)]
struct XhciTrb {
    p0: u32, // parameter lo (or data ptr lo)
    p1: u32, // parameter hi (or data ptr hi)
    p2: u32, // status / length
    p3: u32, // cycle + type + flags
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct XhciSlotCtx {
    f1: u32, f2: u32, f3: u32, f4: u32,
    rsvd: [u32; 4],
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct XhciEpCtx {
    f1: u32, f2: u32,
    deq_lo: u32, // TR dequeue pointer lo + DCS
    deq_hi: u32, // TR dequeue pointer hi
    f5: u32,
    rsvd: [u32; 3],
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct XhciInputCtx {
    drop_flags: u32,
    add_flags: u32,
    rsvd: [u32; 6],
    slot: XhciSlotCtx,
    eps: [XhciEpCtx; 30],
}

const XHCI_CMD_RING_SZ: usize = 64;
const XHCI_EVT_RING_SZ: usize = 64;
const XHCI_TR_RING_SZ:  usize = 32;

pub struct XhciController {
    cap_base: usize,  // capability register base (pointer-sized MMIO)
    op_base:  usize,  // operational register base
    rt_base:  usize,  // runtime register base
    db_base:  usize,  // doorbell array base
    cmd_ring: *mut XhciTrb,
    evt_ring: *mut XhciTrb,
    cmd_enq:  usize,
    cmd_cycle: u8,
    evt_deq:  usize,
    evt_cycle: u8,
    // DCBAA: array of 64-bit (2×u32) entries; we store as *mut u32 and write pairs
    dcbaa:    *mut u32,
    max_slots: u32,
    max_ports: u32,
    kbd_slot:  u8,
    kbd_tr:    *mut XhciTrb,
    kbd_tr_enq: usize,
    kbd_tr_cycle: u8,
    kbd_ep_ctx_idx: u8,
}

impl XhciController {
    /// `mmio_base` is the raw PCI BAR value (pointer-sized).
    pub fn new(mmio_base: usize) -> Self {
        let cap_len = (mmrd(mmio_base, XCAP_CAPLENGTH) & 0xFF) as usize;
        let op_base  = mmio_base + cap_len;
        let db_off   = mmrd(mmio_base, XCAP_DBOFF) as usize;
        let rt_off   = mmrd(mmio_base, XCAP_RTSOFF) as usize;

        Self {
            cap_base: mmio_base,
            op_base,
            rt_base: mmio_base + rt_off,
            db_base: mmio_base + db_off,
            cmd_ring:  core::ptr::null_mut(),
            evt_ring:  core::ptr::null_mut(),
            cmd_enq:   0,
            cmd_cycle:  1,
            evt_deq:   0,
            evt_cycle:  1,
            dcbaa:     core::ptr::null_mut(),
            max_slots: 0,
            max_ports: 0,
            kbd_slot:  0,
            kbd_tr:    core::ptr::null_mut(),
            kbd_tr_enq: 0,
            kbd_tr_cycle: 1,
            kbd_ep_ctx_idx: 3,
        }
    }

    fn post_cmd(&mut self, p0: u32, p1: u32, p2: u32, p3: u32) {
        unsafe {
            let i = self.cmd_enq;
            (*self.cmd_ring.add(i)).p0 = p0;
            (*self.cmd_ring.add(i)).p1 = p1;
            (*self.cmd_ring.add(i)).p2 = p2;
            (*self.cmd_ring.add(i)).p3 = (p3 & !1) | (self.cmd_cycle as u32);
            self.cmd_enq += 1;

            if self.cmd_enq >= XHCI_CMD_RING_SZ - 1 {
                // Write link TRB at the end of the ring
                let link = &mut *self.cmd_ring.add(XHCI_CMD_RING_SZ - 1);
                link.p3 = (link.p3 & !1) | (self.cmd_cycle as u32);
                self.cmd_enq = 0;
                self.cmd_cycle ^= 1;
            }

            // Ring host controller doorbell (slot 0 = command ring)
            mmwr(self.db_base, 0, 0u32);
        }
    }

    fn poll_event(&mut self, expected_type: u32) -> i32 {
        for _ in 0..500 {
            msleep(1);
            unsafe {
                let ev = &*self.evt_ring.add(self.evt_deq);
                let p3 = ev.p3;
                if ((p3 & 1) as u8) != self.evt_cycle {
                    continue;
                }
                let typ = (p3 >> TRB_TYPE_SHIFT) & 0x3F;
                let cc  = (ev.p2 >> 24) & 0xFF;
                self.evt_deq += 1;

                if self.evt_deq >= XHCI_EVT_RING_SZ {
                    self.evt_deq = 0;
                    self.evt_cycle ^= 1;
                }

                // Update Event Ring Dequeue Pointer (ERDP) — 64-bit register at rt+0x38
                let erdp_ptr = self.evt_ring.add(self.evt_deq) as *const u8;
                // Set EHB (bit 3) to clear the Event Handler Busy flag
                let erdp_lo = phys_lo(erdp_ptr) | (1 << 3);
                let erdp_hi = phys_hi(erdp_ptr);
                // Interrupter 0 runtime regs start at rt_base+0x20; ERDP is at +0x18 within them
                mmwr(self.rt_base + 0x20, 0x18, erdp_lo);
                mmwr(self.rt_base + 0x20, 0x1C, erdp_hi);

                if typ == expected_type {
                    return if cc == 1 { 0 } else { -(cc as i32) };
                }
            }
        }
        -1
    }

    fn enable_slot(&mut self) -> i32 {
        self.post_cmd(0, 0, 0, TRB_CMD_ENABLE_SLOT << TRB_TYPE_SHIFT);
        self.poll_event(TRB_EVT_CMD_COMP)
    }

    fn address_device(&mut self, slot: u8, ctx: *const XhciInputCtx) {
        let p0 = phys_lo(ctx as *const u8);
        let p1 = phys_hi(ctx as *const u8);
        let p3 = (TRB_CMD_ADDRESS_DEV << TRB_TYPE_SHIFT) | ((slot as u32) << TRB_SLOT_SHIFT);
        self.post_cmd(p0, p1, 0, p3);
        let _ = self.poll_event(TRB_EVT_CMD_COMP);
    }
}

impl UsbHostController for XhciController {
    fn init(&mut self) -> bool {
        unsafe {
            // Reset controller
            mmwr(self.op_base, XOP_USBCMD, XCMD_HCRST);
            for _ in 0..100 {
                msleep(1);
                if (mmrd(self.op_base, XOP_USBSTS) & XSTS_HCH) == 0 {
                    break;
                }
            }

            // Read HCSPARAMS1 from capability base
            let hcs1 = mmrd(self.cap_base, XCAP_HCSPARAMS1);
            self.max_slots = hcs1 & 0xFF;
            self.max_ports = (hcs1 >> 24) & 0xFF;

            // Configure MaxSlotsEn
            mmwr(self.op_base, XOP_CONFIG, self.max_slots);

            // Allocate structures (page-aligned via our bump allocator)
            self.cmd_ring = alloc(XHCI_CMD_RING_SZ * 16) as *mut XhciTrb;
            self.evt_ring = alloc(XHCI_EVT_RING_SZ * 16) as *mut XhciTrb;
            // DCBAA: (max_slots+1) 64-bit entries → allocate 2×u32 per slot
            self.dcbaa = alloc(((self.max_slots + 1) as usize) * 8) as *mut u32;

            if self.cmd_ring.is_null() || self.evt_ring.is_null() || self.dcbaa.is_null() {
                return false;
            }

            // Zero rings
            for i in 0..XHCI_CMD_RING_SZ {
                *self.cmd_ring.add(i) = XhciTrb { p0: 0, p1: 0, p2: 0, p3: 0 };
            }
            for i in 0..XHCI_EVT_RING_SZ {
                *self.evt_ring.add(i) = XhciTrb { p0: 0, p1: 0, p2: 0, p3: 0 };
            }
            // Zero DCBAA
            for i in 0..(self.max_slots + 1) as usize * 2 {
                *self.dcbaa.add(i) = 0;
            }

            // Write DCBAA pointer (64-bit register)
            let dcbaa_lo = phys_lo(self.dcbaa as *const u8);
            let dcbaa_hi = phys_hi(self.dcbaa as *const u8);
            mmwr(self.op_base, XOP_DCBAAP_LO, dcbaa_lo);
            mmwr(self.op_base, XOP_DCBAAP_HI, dcbaa_hi);

            // Write Command Ring Control Register (64-bit, bit 0 = RCS = 1)
            let crcr_lo = phys_lo(self.cmd_ring as *const u8) | 1; // RCS=1
            let crcr_hi = phys_hi(self.cmd_ring as *const u8);
            mmwr(self.op_base, XOP_CRCR_LO, crcr_lo);
            mmwr(self.op_base, XOP_CRCR_HI, crcr_hi);

            // Set up Event Ring Segment Table (minimal: 1 segment)
            // ERST size=1 → write to ERSTSZ (interrupter 0 at rt+0x20, offset 0x08)
            mmwr(self.rt_base + 0x20, 0x08, 1u32);

            // Write Event Ring Dequeue Pointer (ERDP, 64-bit at rt+0x20+0x18)
            let erdp_lo = phys_lo(self.evt_ring as *const u8);
            let erdp_hi = phys_hi(self.evt_ring as *const u8);
            mmwr(self.rt_base + 0x20, 0x18, erdp_lo);
            mmwr(self.rt_base + 0x20, 0x1C, erdp_hi);

            // Start the controller
            mmwr(self.op_base, XOP_USBCMD, XCMD_RUN);

            true
        }
    }

    fn control_transfer(
        &mut self,
        slot: u8,
        ep: u8,
        setup: &UsbSetupPacket,
        data: &mut [u8],
        is_in: bool,
        _is_ls: bool,
    ) -> i32 {
        unsafe {
            let db_off = (slot as u32) * 4;
            let tr = self.kbd_tr;
            let mut enq = 0usize;
            let cyc = 1u32;

            // Setup TRB — data is inlined (IDT=1), so p0/p1 carry the 8-byte setup packet
            let s0 = (setup.bm_request_type as u32)
                | ((setup.b_request as u32) << 8)
                | ((setup.w_value as u32) << 16);
            let s1 = (setup.w_index as u32) | ((setup.w_length as u32) << 16);
            let trt = if is_in { 3u32 } else { 2u32 };
            let s3 = (TRB_SETUP << TRB_TYPE_SHIFT) | TRB_IDT | (trt << 16) | cyc;

            (*tr.add(enq)).p0 = s0;
            (*tr.add(enq)).p1 = s1;
            (*tr.add(enq)).p2 = 8;
            (*tr.add(enq)).p3 = s3;
            enq += 1;

            if !data.is_empty() {
                let mut d3 = (TRB_DATA << TRB_TYPE_SHIFT) | TRB_ISP | TRB_IOC | cyc;
                if is_in { d3 |= TRB_DIR_IN; }
                (*tr.add(enq)).p0 = phys_lo(data.as_mut_ptr());
                (*tr.add(enq)).p1 = phys_hi(data.as_mut_ptr());
                (*tr.add(enq)).p2 = data.len() as u32;
                (*tr.add(enq)).p3 = d3;
                enq += 1;
            }

            // Status TRB
            let mut st3 = (TRB_STATUS << TRB_TYPE_SHIFT) | TRB_IOC | cyc;
            if data.is_empty() || !is_in { st3 |= TRB_DIR_IN; }
            (*tr.add(enq)).p0 = 0;
            (*tr.add(enq)).p1 = 0;
            (*tr.add(enq)).p2 = 0;
            (*tr.add(enq)).p3 = st3;

            mmwr(self.db_base, db_off, 1u32);

            if self.poll_event(TRB_EVT_TRANSFER) == 0 {
                data.len() as i32
            } else {
                -1
            }
        }
    }

    fn interrupt_in(
        &mut self,
        slot: u8,
        ep_ctx_idx: u8,
        buf: &mut [u8],
        _toggle: &mut u8,
        _is_ls: bool,
    ) -> i32 {
        unsafe {
            let i = self.kbd_tr_enq;
            (*self.kbd_tr.add(i)).p0 = phys_lo(buf.as_mut_ptr());
            (*self.kbd_tr.add(i)).p1 = phys_hi(buf.as_mut_ptr());
            (*self.kbd_tr.add(i)).p2 = buf.len() as u32;
            (*self.kbd_tr.add(i)).p3 =
                (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_ISP | TRB_IOC | (self.kbd_tr_cycle as u32);
            self.kbd_tr_enq += 1;

            if self.kbd_tr_enq >= XHCI_TR_RING_SZ - 1 {
                // Write link TRB pointing back to ring start
                let link = &mut *self.kbd_tr.add(XHCI_TR_RING_SZ - 1);
                link.p0 = phys_lo(self.kbd_tr as *const u8);
                link.p1 = phys_hi(self.kbd_tr as *const u8);
                link.p2 = 0;
                link.p3 = (TRB_LINK << TRB_TYPE_SHIFT) | TRB_TC | (self.kbd_tr_cycle as u32);
                self.kbd_tr_enq = 0;
                self.kbd_tr_cycle ^= 1;
            }

            mmwr(self.db_base, (slot as u32) * 4, ep_ctx_idx as u32);

            if self.poll_event(TRB_EVT_TRANSFER) == 0 {
                buf.len() as i32
            } else {
                -1
            }
        }
    }

    fn get_port_status(&self, port: u8) -> u32 {
        mmrd(self.op_base, XOP_PORTSC + (port as u32) * 0x10)
    }

    fn reset_port(&mut self, port: u8) -> bool {
        let port_reg = XOP_PORTSC + (port as u32) * 0x10;
        mmwr(self.op_base, port_reg, XPS_PR);
        for _ in 0..100 {
            msleep(1);
            if (mmrd(self.op_base, port_reg) & XPS_PR) == 0 {
                break;
            }
        }
        (mmrd(self.op_base, port_reg) & XPS_PED) != 0
    }
}
