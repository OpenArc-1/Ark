#![no_std]

use core::mem::size_of;

#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct UsbSetupPacket {
    pub bm_request_type: u8,
    pub b_request: u8,
    pub w_value: u16,
    pub w_index: u16,
    pub w_length: u16,
}

impl UsbSetupPacket {
    pub fn new(
        direction: UsbDirection,
        request_type: UsbRequestType,
        recipient: UsbRecipient,
        request: u8,
        value: u16,
        index: u16,
        length: u16,
    ) -> Self {
        Self {
            bm_request_type: (direction as u8) | (request_type as u8) | (recipient as u8),
            b_request: request,
            w_value: value,
            w_index: index,
            w_length: length,
        }
    }
}

#[derive(Clone, Copy, Debug)]
#[repr(u8)]
pub enum UsbDirection {
    Out = 0x00,
    In = 0x80,
}

#[derive(Clone, Copy, Debug)]
#[repr(u8)]
pub enum UsbRequestType {
    Standard = 0x00,
    Class = 0x20,
    Vendor = 0x40,
    Reserved = 0x60,
}

#[derive(Clone, Copy, Debug)]
#[repr(u8)]
pub enum UsbRecipient {
    Device = 0x00,
    Interface = 0x01,
    Endpoint = 0x02,
    Other = 0x03,
}

#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct UsbDeviceDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub bcd_usb: u16,
    pub b_device_class: u8,
    pub b_device_sub_class: u8,
    pub b_device_protocol: u8,
    pub b_max_packet_size0: u8,
    pub id_vendor: u16,
    pub id_product: u16,
    pub bcd_device: u16,
    pub i_manufacturer: u8,
    pub i_product: u8,
    pub i_serial_number: u8,
    pub b_num_configurations: u8,
}

#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct UsbInterfaceDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_interface_number: u8,
    pub b_alternate_setting: u8,
    pub b_num_endpoints: u8,
    pub b_interface_class: u8,
    pub b_interface_sub_class: u8,
    pub b_interface_protocol: u8,
    pub i_interface: u8,
}

#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct UsbEndpointDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_endpoint_address: u8,
    pub bm_attributes: u8,
    pub w_max_packet_size: u16,
    pub b_interval: u8,
}

impl UsbEndpointDescriptor {
    pub fn is_in(&self) -> bool {
        (self.b_endpoint_address & 0x80) != 0
    }

    pub fn ep_num(&self) -> u8 {
        self.b_endpoint_address & 0x0F
    }

    pub fn is_interrupt(&self) -> bool {
        (self.bm_attributes & 0x03) == 0x03
    }
}

pub const USB_REQ_GET_DESCRIPTOR: u8 = 0x06;
pub const USB_REQ_SET_ADDRESS: u8 = 0x05;
pub const USB_REQ_SET_CONFIGURATION: u8 = 0x09;
pub const USB_REQ_SET_PROTOCOL: u8 = 0x0B;
pub const USB_REQ_SET_IDLE: u8 = 0x0A;

pub const USB_DESC_DEVICE: u8 = 0x01;
pub const USB_DESC_CONFIG: u8 = 0x02;
pub const USB_DESC_INTERFACE: u8 = 0x04;
pub const USB_DESC_ENDPOINT: u8 = 0x05;

pub const USB_PID_SETUP: u8 = 0x2D;
pub const USB_PID_IN: u8 = 0x69;
pub const USB_PID_OUT: u8 = 0xE1;

pub const USB_CLASS_HID: u8 = 0x03;
pub const USB_SUBCLASS_BOOT: u8 = 0x01;
pub const USB_PROTOCOL_KEYBOARD: u8 = 0x01;
pub const USB_PROTOCOL_MOUSE: u8 = 0x02;
