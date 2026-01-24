# include "ark/printk.h"
# include "pci.h"
# include "../io/built-in.h" // needed for I/O to read and write through ports


// SEND TO ADDRESS PORT(0xCF8) AND READ FROM 0xCFC //
//  NOTE :  0xCF8 --->  ADDRESS PORT  && 0xCFC --> DATA PORT

u32 pciread(u8 bus, u8 slot, u8 func, u8 offset){

    u32 address =
        (1 << 31)    |
        (bus << 16)  |
        (slot << 11) |
        (func << 8)  |
        (offset & 0xFC);

    outl(0xCF8, address); // send address to the 0xCF8

    return inl(0xCFC); // READ IT THROUGH data port(0xCFC)
}

// Get device info if a device exists at this address
bool pci_get_device(u8 bus, u8 slot, u8 func, pci_device_t *dev)
{
    u32 value = pciread(bus, slot, func, 0x00);
    u16 vendor_id = value & 0xFFFF;
    
    if (vendor_id == 0xFFFF) {
        return false;
    }
    
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = (value >> 16) & 0xFFFF;
    
    // Read class, subclass, prog_if from offset 0x08
    u32 class_info = pciread(bus, slot, func, 0x08);
    dev->prog_if = (class_info & 0xFF);
    dev->subclass = (class_info >> 8) & 0xFF;
    dev->class = (class_info >> 16) & 0xFF;
    
    return true;
}

// Read a PCI BAR (Base Address Register)
u32 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar_index)
{
    // BARs are at offsets 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24
    if (bar_index > 5) {
        return 0;
    }
    
    u8 offset = 0x10 + (bar_index * 4);
    return pciread(bus, slot, func, offset);
}

// loop through pci and finding the port
void scanAll(void){
    for (u8 bus = 0; bus < 255; bus++){
        for (u8 slot = 0; slot < 32; slot++){
            for (u8 func = 0; func < 8; func++){
                pci_device_t dev;
                if (pci_get_device(bus, slot, func, &dev)) {
                    printk("PCI: bus: %u  slot: %u  func: %u  Device: %04x:%04x  Class: %02x.%02x.%02x\n",
                           bus, slot, func, dev.vendor_id, dev.device_id,
                           dev.class, dev.subclass, dev.prog_if);
                }
            }
        }
    }
}