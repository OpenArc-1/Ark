#![no_std]

use crate::hcd::traits::{HcType, UsbHostController};
use crate::hcd::{ohci::OhciController, uhci::UhciController, xhci::XhciController};
use crate::kbd;
use crate::usb_types::{
    UsbDirection, UsbRecipient, UsbRequestType, UsbSetupPacket, USB_CLASS_HID, USB_DESC_CONFIG,
    USB_DESC_DEVICE, USB_DESC_ENDPOINT, USB_DESC_INTERFACE, USB_PROTOCOL_KEYBOARD,
    USB_REQ_GET_DESCRIPTOR, USB_REQ_SET_ADDRESS, USB_REQ_SET_CONFIGURATION, USB_REQ_SET_IDLE,
    USB_REQ_SET_PROTOCOL, USB_SUBCLASS_BOOT,
};
use crate::x86_io::{inl, outl};

const PCI_CONFIG_ADDRESS: u16 = 0xCF8;
const PCI_CONFIG_DATA:    u16 = 0xCFC;

const PCI_CLASS_SERIAL:  u32 = 0x0C;
const PCI_SUBCLASS_USB:  u32 = 0x03;

const PCI_PROGIF_UHCI: u32 = 0x00;
const PCI_PROGIF_OHCI: u32 = 0x10;
const PCI_PROGIF_EHCI: u32 = 0x20;
const PCI_PROGIF_XHCI: u32 = 0x30;

// PCI BAR type bits
const PCI_BAR_IO:       u32 = 1 << 0;
const PCI_BAR_MEM_TYPE: u32 = 0x6;
const PCI_BAR_MEM_64:   u32 = 0x4; // bits[2:1] == 10 → 64-bit BAR

fn pci_read(bus: u8, slot: u8, func: u8, offset: u8) -> u32 {
    let address = (1u32 << 31)
        | ((bus as u32) << 16)
        | ((slot as u32) << 11)
        | ((func as u32) << 8)
        | ((offset as u32) & 0xFC);
    unsafe {
        outl(PCI_CONFIG_ADDRESS, address);
        inl(PCI_CONFIG_DATA)
    }
}

/// Read a PCI MMIO BAR and return its base as a pointer-sized integer.
/// Handles both 32-bit and 64-bit BARs transparently.
/// Returns 0 if the BAR is an I/O BAR or unset.
fn pci_read_mmio_bar(bus: u8, slot: u8, func: u8, bar_offset: u8) -> usize {
    let bar0 = pci_read(bus, slot, func, bar_offset);

    // I/O space BAR — not an MMIO BAR
    if (bar0 & PCI_BAR_IO) != 0 {
        return 0;
    }

    let bar_type = (bar0 & PCI_BAR_MEM_TYPE) as u32;
    let base_lo = (bar0 & !0xF) as usize;

    if bar_type == PCI_BAR_MEM_64 {
        // 64-bit BAR: next register holds the high 32 bits
        let bar1 = pci_read(bus, slot, func, bar_offset + 4);
        #[cfg(target_pointer_width = "64")]
        { base_lo | ((bar1 as usize) << 32) }
        #[cfg(not(target_pointer_width = "64"))]
        {
            // On a 32-bit kernel we can only address the low 4 GB.
            // If the high word is non-zero the device is above 4 GB — skip it.
            if bar1 != 0 { 0 } else { base_lo }
        }
    } else {
        // 32-bit BAR
        base_lo
    }
}

/// Read a UHCI I/O BAR (always 16-bit port space).
fn pci_read_io_bar(bus: u8, slot: u8, func: u8, bar_offset: u8) -> u16 {
    let bar = pci_read(bus, slot, func, bar_offset);
    if (bar & PCI_BAR_IO) == 0 { return 0; }
    (bar & 0xFFFC) as u16
}

fn pci_find_usb_controllers() -> [(u8, u8, u8, usize, HcType); 4] {
    let mut controllers = [(0u8, 0u8, 0u8, 0usize, HcType::Xhci); 4];
    let mut count = 0;

    'outer: for bus in 0..=255u8 {
        for slot in 0..32u8 {
            if count >= 4 { break 'outer; }

            let vendor = pci_read(bus, slot, 0, 0) & 0xFFFF;
            if vendor == 0xFFFF { continue; }

            let class_reg = pci_read(bus, slot, 0, 8);
            let class    = (class_reg >> 24) & 0xFF;
            let subclass = (class_reg >> 16) & 0xFF;
            let prog_if  = (class_reg >>  8) & 0xFF;

            if class != PCI_CLASS_SERIAL || subclass != PCI_SUBCLASS_USB {
                continue;
            }

            if prog_if == PCI_PROGIF_XHCI || prog_if == PCI_PROGIF_EHCI || prog_if == PCI_PROGIF_OHCI {
                let hc_type = if prog_if == PCI_PROGIF_OHCI { HcType::Ohci } else { HcType::Xhci };
                let base = pci_read_mmio_bar(bus, slot, 0, 0x10);
                if base != 0 {
                    controllers[count] = (bus, slot, 0, base, hc_type);
                    count += 1;
                }
            } else if prog_if == PCI_PROGIF_UHCI {
                let io_base = pci_read_io_bar(bus, slot, 0, 0x20) as usize;
                if io_base != 0 {
                    controllers[count] = (bus, slot, 0, io_base, HcType::Uhci);
                    count += 1;
                }
            }
        }
    }

    controllers
}

pub struct UsbStack {
    xhci: Option<XhciController>,
    ohci: Option<OhciController>,
    uhci: Option<UhciController>,
    kbd_address: u8,
    kbd_endpoint: u8,
    kbd_max_packet: u16,
    kbd_toggle: u8,
    kbd_controller_type: u8,
    kbd_interval: u8,
}

impl UsbStack {
    pub const fn new() -> Self {
        Self {
            xhci: None,
            ohci: None,
            uhci: None,
            kbd_address: 0,
            kbd_endpoint: 0,
            kbd_max_packet: 8,
            kbd_toggle: 0,
            kbd_controller_type: 0,
            kbd_interval: 10,
        }
    }

    pub fn add_xhci(&mut self, base: usize) {
        self.xhci = Some(XhciController::new(base));
    }

    pub fn add_ohci(&mut self, base: usize) {
        self.ohci = Some(OhciController::new(base));
    }

    pub fn add_uhci(&mut self, base: u16) {
        self.uhci = Some(UhciController::new(base));
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
        match self.kbd_controller_type {
            1 => {
                if let Some(ref mut c) = self.xhci {
                    c.control_transfer(addr, ep, setup, data, is_in, is_ls)
                } else { -1 }
            }
            2 => {
                if let Some(ref mut c) = self.ohci {
                    c.control_transfer(addr, ep, setup, data, is_in, is_ls)
                } else { -1 }
            }
            3 => {
                if let Some(ref mut c) = self.uhci {
                    c.control_transfer(addr, ep, setup, data, is_in, is_ls)
                } else { -1 }
            }
            _ => -1,
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
        match self.kbd_controller_type {
            1 => {
                if let Some(ref mut c) = self.xhci {
                    c.interrupt_in(addr, ep, buf, toggle, is_ls)
                } else { -1 }
            }
            2 => {
                if let Some(ref mut c) = self.ohci {
                    c.interrupt_in(addr, ep, buf, toggle, is_ls)
                } else { -1 }
            }
            3 => {
                if let Some(ref mut c) = self.uhci {
                    c.interrupt_in(addr, ep, buf, toggle, is_ls)
                } else { -1 }
            }
            _ => -1,
        }
    }

    fn get_port_status(&self, port: u8) -> u32 {
        match self.kbd_controller_type {
            1 => { if let Some(ref c) = self.xhci { c.get_port_status(port) } else { 0 } }
            2 => { if let Some(ref c) = self.ohci { c.get_port_status(port) } else { 0 } }
            3 => { if let Some(ref c) = self.uhci { c.get_port_status(port) } else { 0 } }
            _ => 0,
        }
    }

    fn reset_port(&mut self, port: u8) -> bool {
        match self.kbd_controller_type {
            1 => { if let Some(ref mut c) = self.xhci { c.reset_port(port) } else { false } }
            2 => { if let Some(ref mut c) = self.ohci { c.reset_port(port) } else { false } }
            3 => { if let Some(ref mut c) = self.uhci { c.reset_port(port) } else { false } }
            _ => false,
        }
    }

    fn init_controller(&mut self) -> bool {
        match self.kbd_controller_type {
            1 => { if let Some(ref mut c) = self.xhci { c.init() } else { false } }
            2 => { if let Some(ref mut c) = self.ohci { c.init() } else { false } }
            3 => { if let Some(ref mut c) = self.uhci { c.init() } else { false } }
            _ => false,
        }
    }

    fn wait_port_ready(&mut self, port: u8) -> bool {
        for _ in 0..100 {
            let status = self.get_port_status(port);
            if (status & 1) != 0 {
                if self.reset_port(port) {
                    return true;
                }
            }
            msleep(10);
        }
        false
    }

    fn get_device_descriptor(&mut self, addr: u8, is_ls: bool) -> Option<[u8; 8]> {
        let setup = UsbSetupPacket::new(
            UsbDirection::In,
            UsbRequestType::Standard,
            UsbRecipient::Device,
            USB_REQ_GET_DESCRIPTOR,
            (USB_DESC_DEVICE as u16) << 8,
            0,
            8,
        );
        let mut data = [0u8; 8];
        if self.control_transfer(addr, 0, &setup, &mut data, true, is_ls) < 0 {
            return None;
        }
        Some(data)
    }

    fn set_address(&mut self, addr: u8, is_ls: bool) -> bool {
        let setup = UsbSetupPacket::new(
            UsbDirection::Out,
            UsbRequestType::Standard,
            UsbRecipient::Device,
            USB_REQ_SET_ADDRESS,
            addr as u16,
            0,
            0,
        );
        let mut data = [0u8; 0];
        self.control_transfer(0, 0, &setup, &mut data, false, is_ls) >= 0
    }

    fn get_config_descriptor(&mut self, addr: u8, max_len: u16, is_ls: bool) -> Option<[u8; 256]> {
        let setup = UsbSetupPacket::new(
            UsbDirection::In,
            UsbRequestType::Standard,
            UsbRecipient::Device,
            USB_REQ_GET_DESCRIPTOR,
            (USB_DESC_CONFIG as u16) << 8,
            0,
            max_len,
        );
        let mut data = [0u8; 256];
        let len = self.control_transfer(
            addr, 0, &setup, &mut data[..(max_len as usize)], true, is_ls,
        );
        if len < 0 { return None; }
        Some(data)
    }

    fn set_configuration(&mut self, addr: u8, config: u8, is_ls: bool) -> bool {
        let setup = UsbSetupPacket::new(
            UsbDirection::Out,
            UsbRequestType::Standard,
            UsbRecipient::Device,
            USB_REQ_SET_CONFIGURATION,
            config as u16,
            0,
            0,
        );
        let mut data = [0u8; 0];
        self.control_transfer(addr, 0, &setup, &mut data, false, is_ls) >= 0
    }

    fn find_keyboard_endpoint(&self, config_data: &[u8]) -> Option<(u8, u8, u16, u8)> {
        if config_data.len() < 9 { return None; }
        let mut offset = 0;
        while offset + 9 <= config_data.len() {
            let length = config_data[offset] as usize;
            let desc_type = config_data[offset + 1];
            if length < 2 || offset + length > config_data.len() { break; }

            if desc_type == USB_DESC_INTERFACE {
                let intf_class    = config_data[offset + 5];
                let intf_subclass = config_data[offset + 6];
                let intf_protocol = config_data[offset + 7];

                if intf_class == USB_CLASS_HID
                    && intf_subclass == USB_SUBCLASS_BOOT
                    && intf_protocol == USB_PROTOCOL_KEYBOARD
                {
                    offset += length;
                    while offset + 7 <= config_data.len() {
                        let ep_len  = config_data[offset] as usize;
                        let ep_type = config_data[offset + 1];
                        if ep_len < 7 || ep_type != USB_DESC_ENDPOINT { break; }

                        let ep_addr  = config_data[offset + 2];
                        let ep_dir_in = (ep_addr & 0x80) != 0;
                        let ep_num   = ep_addr & 0x0F;
                        let max_pkt  = (config_data[offset + 4] as u16)
                            | ((config_data[offset + 5] as u16) << 8);
                        let interval = config_data[offset + 6];

                        if ep_dir_in && ep_num != 0 {
                            return Some((ep_num, 0, max_pkt, interval));
                        }
                        offset += ep_len;
                    }
                }
            }
            offset += length;
        }
        None
    }

    fn set_idle(&mut self, addr: u8, duration: u8, is_ls: bool) -> bool {
        let setup = UsbSetupPacket::new(
            UsbDirection::Out,
            UsbRequestType::Class,
            UsbRecipient::Interface,
            USB_REQ_SET_IDLE,
            (duration as u16) << 8,
            0,
            0,
        );
        let mut data = [0u8; 0];
        self.control_transfer(addr, 0, &setup, &mut data, false, is_ls) >= 0
    }

    fn set_protocol(&mut self, addr: u8, protocol: u8, is_ls: bool) -> bool {
        let setup = UsbSetupPacket::new(
            UsbDirection::Out,
            UsbRequestType::Class,
            UsbRecipient::Interface,
            USB_REQ_SET_PROTOCOL,
            protocol as u16,
            0,
            0,
        );
        let mut data = [0u8; 0];
        self.control_transfer(addr, 0, &setup, &mut data, false, is_ls) >= 0
    }

    fn enumerate_controller(&mut self, ctrl_type: u8) -> bool {
        self.kbd_controller_type = ctrl_type;
        if !self.init_controller() { return false; }

        for port in 0..16u8 {
            let status = self.get_port_status(port);
            if (status & 1) == 0 { continue; }
            if !self.wait_port_ready(port) { continue; }

            // Bit 9 of port status = low-speed for OHCI/UHCI;
            // for xHCI speed is encoded in bits[13:10] — LS == 1
            let is_ls = match ctrl_type {
                1 => {
                    // xHCI: PORTSC bits[13:10] → speed; 1 = Full, 2 = Low, 3 = High, 4 = Super
                    ((status >> 10) & 0xF) == 2
                }
                _ => (status & (1 << 9)) != 0,
            };

            // SET_ADDRESS to address 1 (address 0 is default after reset)
            if !self.set_address(1, is_ls) { continue; }
            msleep(2);

            let dev_desc = match self.get_device_descriptor(1, is_ls) {
                Some(d) => d,
                None => continue,
            };
            let max_packet0 = dev_desc[7] as u16;

            // Move device to address 2
            if !self.set_address(2, is_ls) { continue; }
            msleep(2);

            // Read total config descriptor length from wTotalLength in device descriptor
            let config_len = {
                let raw = (dev_desc[2] as u16) | ((dev_desc[3] as u16) << 8);
                raw.min(256)
            };

            let config_data = match self.get_config_descriptor(2, config_len, is_ls) {
                Some(c) => c,
                None => continue,
            };

            let (ep_num, _ep_dir, max_pkt, interval) =
                match self.find_keyboard_endpoint(&config_data) {
                    Some(e) => e,
                    None => continue,
                };

            let config_value = config_data[5];
            if !self.set_configuration(2, config_value, is_ls) { continue; }
            msleep(2);

            let _ = self.set_idle(2, 0, is_ls);
            let _ = self.set_protocol(2, 0, is_ls);

            self.kbd_address    = 2;
            self.kbd_endpoint   = ep_num;
            self.kbd_max_packet = max_packet0;
            self.kbd_toggle     = 0;
            self.kbd_interval   = interval;

            return true;
        }
        false
    }

    pub fn init(&mut self) -> bool {
        if self.xhci.is_some() && self.enumerate_controller(1) { return true; }
        self.kbd_controller_type = 0;
        if self.ohci.is_some() && self.enumerate_controller(2) { return true; }
        self.kbd_controller_type = 0;
        if self.uhci.is_some() && self.enumerate_controller(3) { return true; }
        false
    }

    pub fn poll(&mut self) {
        if self.kbd_address == 0 { return; }

        let mut buf = [0u8; 8];
        let is_ls = self.kbd_max_packet <= 8;
        let addr = self.kbd_address;
        let ep   = self.kbd_endpoint;
        let mut toggle_val = self.kbd_toggle;

        let result = self.interrupt_in(addr, ep, &mut buf, &mut toggle_val, is_ls);
        self.kbd_toggle = toggle_val;

        if result > 0 {
            kbd::rust_usb_kbd_decode_report(buf.as_ptr());
        }
    }
}

impl Default for UsbStack {
    fn default() -> Self { Self::new() }
}

fn msleep(ms: u32) {
    for _ in 0..ms * 60000 {
        unsafe { core::arch::asm!("") };
    }
}

static mut USB_STACK: UsbStack = UsbStack::new();

/// C-callable entry point.
///
/// `xhci_base`, `ohci_base` are pointer-sized MMIO addresses passed from C.
/// On a 32-bit build they are `u32`; on a 64-bit build the C side should
/// pass them as `uintptr_t` (matched by `usize` in Rust).
/// `uhci_base` is a 16-bit I/O port, always 32-bit padded in C ABI.
#[no_mangle]
pub extern "C" fn rust_usb_init(xhci_base: usize, ohci_base: usize, uhci_base: u32) -> i32 {
    let stack = unsafe { &raw mut USB_STACK };
    let stack = unsafe { &mut *stack };
    let use_pci_scan = xhci_base == 0 && ohci_base == 0 && uhci_base == 0;

    if use_pci_scan {
        let controllers = pci_find_usb_controllers();
        let mut found = false;
        for i in 0..4 {
            if controllers[i].3 != 0 {
                match controllers[i].4 {
                    HcType::Xhci => stack.add_xhci(controllers[i].3),
                    HcType::Ohci => stack.add_ohci(controllers[i].3),
                    HcType::Uhci => stack.add_uhci(controllers[i].3 as u16),
                }
                found = true;
            }
        }
        if !found { return 0; }
    } else {
        if xhci_base != 0 { stack.add_xhci(xhci_base); }
        if ohci_base  != 0 { stack.add_ohci(ohci_base); }
        if uhci_base  != 0 { stack.add_uhci(uhci_base as u16); }
    }

    if stack.init() {
        kbd::rust_usb_kbd_set_present(true);
        1
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_usb_poll() {
    unsafe { (&raw mut USB_STACK).as_mut().unwrap().poll() };
}
