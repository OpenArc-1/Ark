#ifndef __PCI_CORE_H
#define __PCI_CORE_H

#include "ark/printk.h"
#include "ark/pci.h"
#include "../io/built-in.h"

/* PCI Configuration Space Constants */
#define PCI_CONFIG_ADDRESS_PORT     0xCF8
#define PCI_CONFIG_DATA_PORT        0xCFC
#define PCI_INVALID_VENDOR_ID       0xFFFF
#define PCI_MAX_BUSES               256
#define PCI_MAX_SLOTS               32
#define PCI_MAX_FUNCTIONS           8
#define PCI_MAX_BARS                6

/* PCI Header Offsets */
#define PCI_OFFSET_VENDOR_DEVICE    0x00
#define PCI_OFFSET_CLASS_REVISION   0x08
#define PCI_OFFSET_BAR0             0x10
#define PCI_BAR_SIZE                sizeof(u32)

/* PCI Class/Subclass Codes */
#define PCI_CLASS_SERIAL_BUS        0x0C
#define PCI_SUBCLASS_USB            0x03

/* PCI Configuration Address Format */
#define PCI_ADDRESS_ENABLE_BIT      (1 << 31)

/* Device Information Structure */
typedef struct {
    u8 bus;
    u8 slot;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision;
    u32 bars[PCI_MAX_BARS];
} pci_device_info_t;

/**
 * @brief Read a 32-bit value from PCI configuration space
 * 
 * @param bus Bus number (0-255)
 * @param slot Slot number (0-31)
 * @param function Function number (0-7)
 * @param offset Configuration register offset (must be 32-bit aligned)
 * @return u32 Value read from configuration space
 * 
 * @note The offset must be 32-bit aligned (bits 1:0 = 00)
 * @note Uses Type 1 PCI Configuration Mechanism
 */
static inline u32 pci_config_read_u32(u8 bus, u8 slot, u8 function, u8 offset)
{
    /* Ensure offset is 32-bit aligned */
    if (offset & 0x03) {
        printk(KERN_WARNING "PCI: Unaligned read offset 0x%02x\n", offset);
        offset &= 0xFC;
    }
    
    /* Construct configuration address */
    u32 address = PCI_ADDRESS_ENABLE_BIT |
                  ((u32)bus << 16)       |
                  ((u32)slot << 11)      |
                  ((u32)function << 8)   |
                  (offset & 0xFC);
    
    /* Write address and read data */
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

/**
 * @brief Write a 32-bit value to PCI configuration space
 * 
 * @param bus Bus number
 * @param slot Slot number
 * @param function Function number
 * @param offset Configuration register offset
 * @param value Value to write
 */
static inline void pci_config_write_u32(u8 bus, u8 slot, u8 function, 
                                       u8 offset, u32 value)
{
    u32 address = PCI_ADDRESS_ENABLE_BIT |
                  ((u32)bus << 16)       |
                  ((u32)slot << 11)      |
                  ((u32)function << 8)   |
                  (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    outl(PCI_CONFIG_DATA_PORT, value);
}

/**
 * @brief Check if a PCI device exists at the given address
 * 
 * @param bus Bus number
 * @param slot Slot number
 * @param function Function number
 * @return true Device exists
 * @return false No device at this address
 */
static inline bool pci_device_exists(u8 bus, u8 slot, u8 function)
{
    u32 vendor_device = pci_config_read_u32(bus, slot, function, PCI_OFFSET_VENDOR_DEVICE);
    return (vendor_device & 0xFFFF) != PCI_INVALID_VENDOR_ID;
}

/**
 * @brief Get detailed information about a PCI device
 * 
 * @param bus Bus number
 * @param slot Slot number
 * @param function Function number
 * @param info Pointer to structure to fill with device info
 * @return true Successfully retrieved device info
 * @return false Device doesn't exist or info is invalid
 */
bool pci_get_device_info(u8 bus, u8 slot, u8 function, pci_device_info_t *info)
{
    /* Check if device exists */
    if (!pci_device_exists(bus, slot, function)) {
        return false;
    }
    
    /* Read vendor and device ID */
    u32 vendor_device = pci_config_read_u32(bus, slot, function, PCI_OFFSET_VENDOR_DEVICE);
    info->vendor_id = vendor_device & 0xFFFF;
    info->device_id = (vendor_device >> 16) & 0xFFFF;
    
    /* Read class, subclass, prog_if, and revision */
    u32 class_revision = pci_config_read_u32(bus, slot, function, PCI_OFFSET_CLASS_REVISION);
    info->revision = class_revision & 0xFF;
    info->prog_if = (class_revision >> 8) & 0xFF;
    info->subclass = (class_revision >> 16) & 0xFF;
    info->class_code = (class_revision >> 24) & 0xFF;
    
    /* Store address information */
    info->bus = bus;
    info->slot = slot;
    info->function = function;
    
    /* Read all Base Address Registers */
    for (int i = 0; i < PCI_MAX_BARS; i++) {
        info->bars[i] = pci_config_read_u32(bus, slot, function, PCI_OFFSET_BAR0 + (i * 4));
    }
    
    return true;
}

/**
 * @brief Scan all PCI devices and print information
 * 
 * This function scans the entire PCI bus hierarchy and prints information
 * about all detected devices.
 */
void pci_scan_all(void)
{
    printk(KERN_INFO "PCI: Scanning all devices...\n");
    
    int device_count = 0;
    for (u16 bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (u8 slot = 0; slot < PCI_MAX_SLOTS; slot++) {
            /* Check function 0 first to see if device exists */
            if (!pci_device_exists(bus, slot, 0)) {
                continue;
            }
            
            /* Check all functions for this device */
            for (u8 function = 0; function < PCI_MAX_FUNCTIONS; function++) {
                pci_device_info_t info;
                
                if (pci_get_device_info(bus, slot, function, &info)) {
                    device_count++;
                    
                    printk(KERN_INFO "PCI: %02x:%02x.%x %04x:%04x "
                           "Class: %02x Subclass: %02x ProgIf: %02x Rev: %02x\n",
                           bus, slot, function,
                           info.vendor_id, info.device_id,
                           info.class_code, info.subclass, 
                           info.prog_if, info.revision);
                }
            }
        }
    }
    
    printk(KERN_INFO "PCI: Found %d device(s)\n", device_count);
}

/**
 * @brief Scan for USB controllers and display detailed information
 * 
 * This function specifically looks for USB controllers (class 0x0C, subclass 0x03)
 * and displays detailed information about them.
 */
void pci_scan_usb_controllers(void)
{
    printk(KERN_INFO "PCI: Scanning for USB controllers...\n");
    
    int usb_controller_count = 0;
    for (u16 bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (u8 slot = 0; slot < PCI_MAX_SLOTS; slot++) {
            for (u8 function = 0; function < PCI_MAX_FUNCTIONS; function++) {
                pci_device_info_t info;
                
                if (!pci_get_device_info(bus, slot, function, &info)) {
                    continue;
                }
                
                /* Check if this is a USB controller */
                if (info.class_code == PCI_CLASS_SERIAL_BUS && 
                    info.subclass == PCI_SUBCLASS_USB) {
                    
                    usb_controller_count++;
                    
                    printk(KERN_INFO "USB Controller %d:\n", usb_controller_count);
                    printk(KERN_INFO "  Location:    %02x:%02x.%x\n", 
                           bus, slot, function);
                    printk(KERN_INFO "  Vendor:      0x%04x\n", info.vendor_id);
                    printk(KERN_INFO "  Device:      0x%04x\n", info.device_id);
                    printk(KERN_INFO "  ProgIf:      0x%02x\n", info.prog_if);
                    printk(KERN_INFO "  Revision:    0x%02x\n", info.revision);
                    
                    /* Print BAR information */
                    for (int i = 0; i < PCI_MAX_BARS; i++) {
                        if (info.bars[i] != 0 && info.bars[i] != 0xFFFFFFFF) {
                            printk(KERN_INFO "  BAR%d:        0x%08x\n", i, info.bars[i]);
                        }
                    }
                    
                    printk(KERN_INFO "\n");
                }
            }
        }
    }
    
    if (usb_controller_count == 0) {
        printk(KERN_WARNING "PCI: No USB controllers found\n");
        printk(KERN_WARNING "  Possible reasons:\n");
        printk(KERN_WARNING "  1. USB may be disabled in BIOS/UEFI\n");
        printk(KERN_WARNING "  2. System may not have USB controllers\n");
        printk(KERN_WARNING "  3. Try running pci_scan_all() to see all devices\n");
    } else {
        printk(KERN_INFO "PCI: Found %d USB controller(s)\n", usb_controller_count);
    }
}

/**
 * @brief Find a specific PCI device by vendor and device ID
 * 
 * @param vendor_id Vendor ID to search for
 * @param device_id Device ID to search for
 * @param info Pointer to structure to fill with device info
 * @return true Device found
 * @return false Device not found
 */
bool pci_find_device_by_id(u16 vendor_id, u16 device_id, pci_device_info_t *info)
{
    for (u16 bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (u8 slot = 0; slot < PCI_MAX_SLOTS; slot++) {
            for (u8 function = 0; function < PCI_MAX_FUNCTIONS; function++) {
                if (!pci_get_device_info(bus, slot, function, info)) {
                    continue;
                }
                
                if (info->vendor_id == vendor_id && info->device_id == device_id) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

/**
 * @brief Read a specific BAR (Base Address Register)
 * 
 * @param bus Bus number
 * @param slot Slot number
 * @param function Function number
 * @param bar_index BAR index (0-5)
 * @return u32 BAR value, 0 if invalid index
 */
u32 pci_read_bar(u8 bus, u8 slot, u8 function, u8 bar_index)
{
    if (bar_index >= PCI_MAX_BARS) {
        printk(KERN_WARNING "PCI: Invalid BAR index %u\n", bar_index);
        return 0;
    }
    
    return pci_config_read_u32(bus, slot, function, PCI_OFFSET_BAR0 + (bar_index * 4));
}

#endif /* __PCI_CORE_H */
