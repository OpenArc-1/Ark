#ifndef PCI_H
#define PCI_H
#pragma once

#include "ark/types.h"

typedef struct {
    u8 bus;
    u8 slot;
    u8 func;
    u16 vendor_id;
    u16 device_id;
    u8 class;
    u8 subclass;
    u8 prog_if;
} pci_device_t;

// reading a 32 bit PCI register to find the device
u32 pciread(u8 bus, u8 slot, u8 func, u8 offset);

// scanning looping in PCI
void scanAll(void);

// Scan specifically for USB controllers with detailed output
void scan_usb_controllers(void);

// read a PCI BAR (Base Address Register)
u32 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar_index);

// macro to iterate through all PCI devices
#define for_each_pci_device(dev) \
    for (int _bus = 0; _bus < 256; _bus++) \
    for (u8 _slot = 0; _slot < 32; _slot++) \
    for (u8 _func = 0; _func < 8; _func++) \
    if (pci_get_device((u8)_bus, _slot, _func, &dev))

// get PCI device info if device exists at this address
bool pci_get_device(u8 bus, u8 slot, u8 func, pci_device_t *dev);

#endif