#![no_std]

use crate::usb_types::UsbSetupPacket;

pub trait UsbHostController {
    fn control_transfer(
        &mut self,
        addr: u8,
        ep: u8,
        setup: &UsbSetupPacket,
        data: &mut [u8],
        is_in: bool,
        is_ls: bool,
    ) -> i32;
    fn interrupt_in(
        &mut self,
        addr: u8,
        ep: u8,
        buf: &mut [u8],
        toggle: &mut u8,
        is_ls: bool,
    ) -> i32;
    fn get_port_status(&self, port: u8) -> u32;
    fn reset_port(&mut self, port: u8) -> bool;
    fn init(&mut self) -> bool;
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum HcType {
    Uhci,
    Ohci,
    Xhci,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PortSpeed {
    Low,
    Full,
    High,
    Super,
}

pub struct PortInfo {
    pub present: bool,
    pub enabled: bool,
    pub speed: PortSpeed,
}
