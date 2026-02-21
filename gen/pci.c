#include "ark/printk.h"
#include "ark/pci.h"
#include "../io/built-in.h"

// =============================================================================
// gen/pci.c — PCI bus driver for ARK kernel
// =============================================================================

// -----------------------------------------------------------------------------
// pciread — Read 32-bit dword from PCI configuration space
//
// BUG FIXED: All fields MUST be cast to u32 before shifting.
//   u8 << 16 on i386: the u8 gets promoted to int (32-bit) but the compiler
//   is free to keep it narrow — result is zero or garbage in the high bits.
//   The address sent to 0xCF8 was wrong → hardware never matched any device.
//
// PCI CONFIG_ADDRESS format (port 0xCF8):
//   bit  31     = enable bit (must be 1)
//   bits [23:16] = bus number
//   bits [15:11] = device/slot number
//   bits [10:8]  = function number
//   bits [7:2]   = register offset (dword aligned, lower 2 bits = 0)
// -----------------------------------------------------------------------------

u32 pciread(u8 bus, u8 slot, u8 func, u8 offset)
{
    u32 address =
        (u32)(1U       << 31) |   // enable bit
        ((u32)bus      << 16) |   // FIXED: cast before shift
        ((u32)slot     << 11) |   // FIXED: cast before shift
        ((u32)func     <<  8) |   // FIXED: cast before shift
        ((u32)offset & 0xFC);     // mask lower 2 bits (dword aligned)

    outl(0xCF8, address);
    return inl(0xCFC);
}

// -----------------------------------------------------------------------------
// pci_get_device — Fill pci_device_t if a device exists at bus/slot/func
//
// BUG FIXED: Class info register (offset 0x08) byte layout:
//   bits [31:24] = Class code    → needs >> 24  (was >> 16 — WRONG)
//   bits [23:16] = Subclass      → needs >> 16  (was >> 8  — WRONG)
//   bits [15:8]  = Prog IF       → needs >> 8   (was >> 0  — WRONG)
//   bits [7:0]   = Revision ID   (not stored in pci_device_t)
//
// With the old wrong shifts, ALL class/subclass values were misread,
// so no device ever matched class=0x02 (Ethernet), class=0x0C (USB), etc.
// -----------------------------------------------------------------------------

bool pci_get_device(u8 bus, u8 slot, u8 func, pci_device_t *dev)
{
    u32 value     = pciread(bus, slot, func, 0x00);
    u16 vendor_id = (u16)(value & 0xFFFF);

    if (vendor_id == 0xFFFF)   // no device present
        return false;

    dev->bus       = bus;
    dev->slot      = slot;
    dev->func      = func;
    dev->vendor_id = vendor_id;
    dev->device_id = (u16)((value >> 16) & 0xFFFF);

    u32 class_info  = pciread(bus, slot, func, 0x08);
    dev->class      = (u8)((class_info >> 24) & 0xFF);  // FIXED: was >> 16
    dev->subclass   = (u8)((class_info >> 16) & 0xFF);  // FIXED: was >> 8
    dev->prog_if    = (u8)((class_info >>  8) & 0xFF);  // FIXED: was >> 0

    return true;
}

// -----------------------------------------------------------------------------
// pci_read_bar — Read Base Address Register (BAR0–BAR5)
// BARs live at config offsets 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24
// -----------------------------------------------------------------------------

u32 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar_index)
{
    if (bar_index > 5)
        return 0;

    u8 offset = 0x10 + (bar_index * 4);
    return pciread(bus, slot, func, offset);
}

// -----------------------------------------------------------------------------
// scanAll — Print every PCI device found on the bus
//
// IMPROVED: Checks header type (offset 0x0C bits[23:16]) before scanning
//   functions 1–7. Bit 7 of header type = multifunction device.
//   Without this, scanning all 8 functions for every slot wastes time
//   and may produce spurious reads on single-function devices.
// -----------------------------------------------------------------------------

void scanAll(void)
{
    printk(T,"[PCI] Starting full bus scan...\n");
    int total = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {

            pci_device_t dev;

            // Check function 0 first — if absent, skip the whole slot
            if (!pci_get_device((u8)bus, slot, 0, &dev))
                continue;

            printk(T,"%04x:%04x  class=%02x sub=%02x pi=%02x\n",
                   bus, slot,
                   (u32)dev.vendor_id, (u32)dev.device_id,
                   (u32)dev.class,     (u32)dev.subclass, (u32)dev.prog_if);
            total++;

            // Read header type to detect multifunction devices (bit 7)
            u32 hdr   = pciread((u8)bus, slot, 0, 0x0C);
            u8  htype = (u8)((hdr >> 16) & 0xFF);

            if (!(htype & 0x80))
                continue;   // single-function device — done with this slot

            for (u8 func = 1; func < 8; func++) {
                if (!pci_get_device((u8)bus, slot, func, &dev))
                    continue;

                printk(T,"[%02x:%02x.%x] %04x:%04x  class=%02x sub=%02x pi=%02x\n",
                       bus, slot, (u32)func,
                       (u32)dev.vendor_id, (u32)dev.device_id,
                       (u32)dev.class,     (u32)dev.subclass, (u32)dev.prog_if);
                total++;
            }
        }
    }

    if (total == 0)
        printk(T,"[PCI] No devices found — check outl/inl in io/built-in.h\n");
    else
        printk(T,"[PCI] Scan complete. Total devices: %d\n", total);
}

// -----------------------------------------------------------------------------
// scan_usb_controllers — Find USB controllers (class=0x0C subclass=0x03)
// -----------------------------------------------------------------------------

void scan_usb_controllers(void)
{
    printk(T,"[USB] Scanning for USB controllers...\n");
    int found = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            pci_device_t dev;
            if (!pci_get_device((u8)bus, slot, 0, &dev))
                continue;

            u32 hdr      = pciread((u8)bus, slot, 0, 0x0C);
            u8  htype    = (u8)((hdr >> 16) & 0xFF);
            u8  max_func = (htype & 0x80) ? 8 : 1;

            for (u8 func = 0; func < max_func; func++) {
                if (!pci_get_device((u8)bus, slot, func, &dev))
                    continue;
                if (dev.class != 0x0C || dev.subclass != 0x03)
                    continue;

                const char *type = "Unknown";
                if      (dev.prog_if == 0x00) type = "UHCI";
                else if (dev.prog_if == 0x10) type = "OHCI";
                else if (dev.prog_if == 0x20) type = "EHCI";
                else if (dev.prog_if == 0x30) type = "xHCI";

                printk(T,"USB-%s  %04x:%04x  BAR0=0x%08x\n",
                       bus, slot, (u32)func, type,
                       (u32)dev.vendor_id, (u32)dev.device_id,
                       pci_read_bar((u8)bus, slot, func, 0));
                found++;
            }
        }
    }

    printk(T,"[USB] Found %d USB controller(s).\n", found);
}