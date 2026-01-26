# include "ark/printk.h"
# include "ark/pci.h"
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
    for (int bus = 0; bus < 256; bus++){
        for (u8 slot = 0; slot < 32; slot++){
            for (u8 func = 0; func < 8; func++){
                pci_device_t dev;
                if (pci_get_device((u8)bus, slot, func, &dev)) {
                    printk("PCI: bus: %u  slot: %u  func: %u  Device: %04x:%04x  Class: %02x.%02x.%02x\n",
                           bus, slot, func, dev.vendor_id, dev.device_id,
                           dev.class, dev.subclass, dev.prog_if);
                }
            }
        }
    }
}

// Find USB controllers and show detailed info
void scan_usb_controllers(void) {
    printk("USB Controller Scan:\n");
    printk("Looking for class=0x0C (Serial), subclass=0x03 (USB)...\n\n");
    
    int found = 0;
    for (int bus = 0; bus < 256; bus++){
        for (u8 slot = 0; slot < 32; slot++){
            for (u8 func = 0; func < 8; func++){
                pci_device_t dev;
                if (pci_get_device((u8)bus, slot, func, &dev)) {
                    // Show serial devices
                    if (dev.class == 0x0C) {
                        printk("PCI SERIAL DEVICE: %02x:%02x.%x | %04x:%04x | Subclass: 0x%02x\n",
                               bus, slot, func, dev.vendor_id, dev.device_id, dev.subclass);
                        
                        // If it's USB, show details
                        if (dev.subclass == 0x03) {
                            printk("  -> USB Controller (prog_if: 0x%02x)\n", dev.prog_if);
                            u32 bar0 = pci_read_bar((u8)bus, slot, func, 0);
                            printk("  -> BAR0: 0x%x\n", bar0);
                            found++;
                        }
                    }
                }
            }
        }
    }
    
    if (found == 0) {
        printk("\nNo USB controllers found! Check:\n");
        printk("  1. Run 'scanAll()' to see all PCI devices\n");
        printk("  2. USB might be disabled in BIOS/UEFI\n");
        printk("  3. USB might use non-standard PCI class codes\n");
    }
}