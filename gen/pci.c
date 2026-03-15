#include "ark/printk.h"
#include "ark/pci.h"
#include "../io/built-in.h"

/* =============================================================================
 * gen/pci.c — PCI bus driver for ARK kernel
 * Credit: Yahya Mokhlis
 * ============================================================================= */

u32 pciread(u8 bus, u8 slot, u8 func, u8 offset)
{
    u32 address =
        (1U        << 31) |
        ((u32)bus  << 16) |
        ((u32)slot << 11) |
        ((u32)func <<  8) |
        ((u32)offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

u32 pciwrite(u8 bus, u8 slot, u8 func, u8 offset, u32 value)
{
    u32 address =
        (1U        << 31) |
        ((u32)bus  << 16) |
        ((u32)slot << 11) |
        ((u32)func <<  8) |
        ((u32)offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC, value);
    return value;
}

bool pci_get_device(u8 bus, u8 slot, u8 func, pci_device_t *dev)
{
    u32 value     = pciread(bus, slot, func, 0x00);
    u16 vendor_id = (u16)(value & 0xFFFF);
    if (vendor_id == 0xFFFF) return false;

    dev->bus       = bus;
    dev->slot      = slot;
    dev->func      = func;
    dev->vendor_id = vendor_id;
    dev->device_id = (u16)((value >> 16) & 0xFFFF);

    u32 ci        = pciread(bus, slot, func, 0x08);
    dev->class    = (u8)((ci >> 24) & 0xFF);
    dev->subclass = (u8)((ci >> 16) & 0xFF);
    dev->prog_if  = (u8)((ci >>  8) & 0xFF);
    return true;
}

u32 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar_index)
{
    if (bar_index > 5) return 0;
    return pciread(bus, slot, func, 0x10 + (bar_index * 4));
}

static const char *pci_vendor_name(u16 vid)
{
    switch (vid) {
    case 0x8086: return "Intel";
    case 0x1022: return "AMD";
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD/ATI";
    case 0x14E4: return "Broadcom";
    case 0x1AF4: return "VirtIO/Red Hat";
    case 0x1B36: return "QEMU";
    case 0x1234: return "QEMU Std";
    case 0x10EC: return "Realtek";
    case 0x104C: return "Texas Instruments";
    case 0x1106: return "VIA";
    case 0x10B9: return "ALi";
    case 0x1039: return "SiS";
    case 0x15AD: return "VMware";
    case 0x80EE: return "VirtualBox";
    case 0x1AE0: return "Google";
    default:     return NULL;
    }
}

static const char *pci_device_name(u16 vid, u16 did)
{
    /* Intel */
    if (vid == 0x8086) {
        switch (did) {
        case 0x1237: return "440FX PCI & Memory Ctrl";
        case 0x7000: return "PIIX3 ISA Bridge";
        case 0x7010: return "PIIX3 IDE Controller";
        case 0x7020: return "PIIX3 USB Controller";
        case 0x7113: return "PIIX4 ACPI";
        case 0x100E: return "82540EM Gigabit Ethernet (e1000)";
        case 0x10D3: return "82574L Gigabit Ethernet";
        case 0x2415: return "AC97 Audio Controller";
        case 0x2668: return "HD Audio Controller";
        case 0x29C0: return "Q35 Host Bridge";
        case 0x2918: return "ICH9 LPC Interface";
        case 0x2922: return "ICH9 AHCI SATA Controller";
        case 0x2930: return "ICH9 SMBus Controller";
        case 0x2934: return "ICH9 USB UHCI Controller";
        case 0x2935: return "ICH9 USB UHCI Controller";
        case 0x2936: return "ICH9 USB UHCI Controller";
        case 0x293A: return "ICH9 USB EHCI Controller";
        case 0x10F5: return "82567LM Gigabit Ethernet";
        /* Haswell (4th gen Core) DRAM controller */
        case 0x0C00: return "Haswell DRAM Controller";
        case 0x0C04: return "Haswell DRAM Controller";
        case 0x0C08: return "Haswell DRAM Controller";
        case 0x0C0C: return "Haswell DRAM Controller";
        case 0x0410: return "Xeon E3-1200 v3 DRAM";
        /* Haswell integrated GPU */
        case 0x0402: return "Haswell GT1 Desktop GPU";
        case 0x0412: return "Haswell GT2 Desktop GPU";
        case 0x0422: return "Haswell GT3 Desktop GPU";
        case 0x0406: return "Haswell GT1 Mobile GPU";
        case 0x0416: return "Haswell GT2 Mobile GPU";
        case 0x041E: return "Haswell GT1 (Celeron/Pentium) GPU";
        /* 8 Series / C220 PCH (Lynx Point) -- H87, B85, Z87 etc. */
        case 0x8C10: return "8 Series PCH PCIe x1 #1";
        case 0x8C14: return "8 Series PCH PCIe x1 #2";
        case 0x8C18: return "8 Series PCH PCIe x1 #3";
        case 0x8C1C: return "8 Series PCH PCIe x1 #4";
        case 0x8C20: return "8 Series PCH HD Audio";
        case 0x8C22: return "8 Series PCH SMBus";
        case 0x8C26: return "8 Series PCH Thermal";
        case 0x8C2D: return "8 Series PCH USB2 EHCI #2";
        case 0x8C31: return "8 Series PCH xHCI USB3";
        case 0x8C3A: return "8 Series PCH MEI #1";
        case 0x8C3B: return "8 Series PCH MEI #2";
        case 0x8C41: return "8 Series PCH LPC (H87)";
        case 0x8C42: return "8 Series PCH LPC (Z87)";
        case 0x8C44: return "8 Series PCH LPC (H81)";
        case 0x8C46: return "8 Series PCH LPC (Z85)";
        case 0x8C49: return "8 Series PCH LPC (B85)";
        case 0x8C4B: return "8 Series PCH LPC (Q85)";
        case 0x8C4F: return "8 Series PCH LPC";
        case 0x8C02: return "8 Series PCH SATA AHCI";
        /* 9 Series PCH (Broadwell / Haswell Refresh) */
        case 0x9C41: return "9 Series PCH LPC";
        case 0x9C20: return "9 Series PCH HD Audio";
        case 0x9C31: return "9 Series PCH xHCI";
        /* Real Intel NICs on Haswell boards */
        case 0x153A: return "I217-LM Gigabit Ethernet";
        case 0x153B: return "I217-V Gigabit Ethernet";
        case 0x1503: return "I210 Gigabit Ethernet";
        case 0x15A1: return "I218-LM Gigabit Ethernet";
        case 0x15A2: return "I218-V Gigabit Ethernet";
        }
    }
    /* QEMU/VirtIO */
    if (vid == 0x1AF4) {
        switch (did) {
        case 0x1000: return "VirtIO Network Device";
        case 0x1001: return "VirtIO Block Device";
        case 0x1002: return "VirtIO Memory Balloon";
        case 0x1003: return "VirtIO Console";
        case 0x1004: return "VirtIO SCSI";
        case 0x1009: return "VirtIO Filesystem";
        case 0x1050: return "VirtIO GPU";
        case 0x1052: return "VirtIO Input Device";
        }
    }
    /* QEMU standard */
    if (vid == 0x1234 && did == 0x1111) return "QEMU/Bochs VGA";
    if (vid == 0x1B36) {
        switch (did) {
        case 0x000D: return "QEMU XHCI USB Controller";
        case 0x0001: return "QEMU PCI-PCI Bridge";
        case 0x0002: return "QEMU PCIE Bridge";
        }
    }
    /* Realtek */
    if (vid == 0x10EC) {
        switch (did) {
        case 0x8139: return "RTL8139 Fast Ethernet";
        case 0x8168: return "RTL8111 Gigabit Ethernet";
        }
    }
    /* VMware */
    if (vid == 0x15AD) {
        switch (did) {
        case 0x0405: return "SVGA II Adapter";
        case 0x0790: return "VMXNET3 Ethernet";
        case 0x07B0: return "VMXNET3 Ethernet";
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------------
 * Class name
 * ----------------------------------------------------------------------------- */
static const char *pci_class_name(u8 class, u8 subclass)
{
    switch (class) {
    case 0x00: return subclass == 0x01 ? "VGA-Compat"      : "Unclassified";
    case 0x01:
        switch (subclass) {
        case 0x00: return "SCSI Controller";
        case 0x01: return "IDE Controller";
        case 0x05: return "ATA Controller";
        case 0x06: return "SATA Controller (AHCI)";
        case 0x08: return "NVMe Controller";
        default:   return "Mass Storage";
        }
    case 0x02:
        switch (subclass) {
        case 0x00: return "Ethernet Controller";
        case 0x80: return "Network Controller";
        default:   return "Network Controller";
        }
    case 0x03:
        switch (subclass) {
        case 0x00: return "VGA Controller";
        case 0x01: return "XGA Controller";
        case 0x02: return "3D Controller";
        default:   return "Display Controller";
        }
    case 0x04: return "Multimedia Controller";
    case 0x05: return "Memory Controller";
    case 0x06:
        switch (subclass) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        case 0x04: return "PCI-PCI Bridge";
        case 0x80: return "Other Bridge";
        default:   return "Bridge";
        }
    case 0x07: return "Serial Comm Controller";
    case 0x08:
        switch (subclass) {
        case 0x00: return "PIC";
        case 0x01: return "DMA Controller";
        case 0x02: return "Timer";
        case 0x03: return "RTC Controller";
        default:   return "System Peripheral";
        }
    case 0x09: return "Input Device Controller";
    case 0x0B: return "Processor";
    case 0x0C:
        switch (subclass) {
        case 0x00: return "FireWire Controller";
        case 0x03:
            switch (subclass) {
            default: return "USB Controller";
            }
        case 0x05: return "SMBus Controller";
        default:   return "Serial Bus Controller";
        }
    case 0x0D: return "Wireless Controller";
    case 0x0E: return "Intelligent Controller";
    case 0x0F: return "Satellite Controller";
    case 0x10: return "Encryption Controller";
    case 0x11: return "Signal Processing Controller";
    case 0x12: return "Processing Accelerator";
    case 0x13: return "Non-Essential Instrumentation";
    case 0xFF: return "Unassigned";
    default:   return "Unknown Device";
    }
}

/* USB prog-if type */
static const char *usb_type(u8 prog_if)
{
    switch (prog_if) {
    case 0x00: return "UHCI";
    case 0x10: return "OHCI";
    case 0x20: return "EHCI";
    case 0x30: return "xHCI";
    default:   return "USB";
    }
}

/* -----------------------------------------------------------------------------
 * Print BARs — only for non-bridge devices to avoid flooding the log
 * ----------------------------------------------------------------------------- */
static void pci_print_bars(u8 bus, u8 slot, u8 func, u8 htype)
{
    /* PCI-PCI bridges (htype 1) and CardBus bridges (htype 2) use a
     * different config space layout; their BARs are not at 0x10..0x24.
     * Printing them would show misleading addresses, so skip them. */
    if ((htype & 0x7F) != 0) return;

    for (u8 i = 0; i < 6; i++) {
        u32 bar = pci_read_bar(bus, slot, func, i);
        if (bar == 0 || bar == 0xFFFFFFFF) continue;
        if (bar & 0x1) {
            u32 port = bar & ~0x3u;
            if (port == 0) continue;
            printk(T, "      BAR%u  IO   port=0x%04x\n", (u32)i, port);
        } else {
            u32 addr = bar & ~0xFu;
            if (addr == 0) continue;
            u8 prefetch = (u8)((bar >> 3) & 0x1);
            printk(T, "      BAR%u  MEM  addr=0x%08x%s\n",
                   (u32)i, addr,
                   prefetch ? " [pf]" : "");
        }
    }
}

/* -----------------------------------------------------------------------------
 * Print one device line — guaranteed no NULL passed to %s
 * ----------------------------------------------------------------------------- */
static void pci_print_device(u8 bus, u8 slot, u8 func, const pci_device_t *dev)
{
    const char *vendor  = pci_vendor_name(dev->vendor_id);
    const char *devname = pci_device_name(dev->vendor_id, dev->device_id);
    const char *cname   = pci_class_name(dev->class, dev->subclass);

    /* cname is now always non-NULL (fixed above).
     * vendor and devname can still be NULL — handle all cases explicitly. */
    if (!vendor)  vendor  = "Unknown";
    if (!cname)   cname   = "Unknown Device";

    if (devname) {
        printk(T, "[%02x:%02x.%x] %s %s\n",
               (u32)bus, (u32)slot, (u32)func,
               vendor, devname);
    } else {
        /* Unknown device — show vendor name (or hex) + raw device ID + class */
        const char *vid_str = pci_vendor_name(dev->vendor_id);
        if (vid_str)
            printk(T, "[%02x:%02x.%x] %s %04x  [%s]\n",
                   (u32)bus, (u32)slot, (u32)func,
                   vid_str, (u32)dev->device_id, cname);
        else
            printk(T, "[%02x:%02x.%x] %04x:%04x  [%s]\n",
                   (u32)bus, (u32)slot, (u32)func,
                   (u32)dev->vendor_id, (u32)dev->device_id,
                   cname);
    }
}

/* -----------------------------------------------------------------------------
 * scanAll — enumerate all PCI buses (0-255) and devices
 * ----------------------------------------------------------------------------- */
void scanAll(void)
{
    printk(T, "[PCI] Scanning bus...\n");
    int total = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            pci_device_t dev;
            if (!pci_get_device((u8)bus, slot, 0, &dev)) continue;

            u32 hdr   = pciread((u8)bus, slot, 0, 0x0C);
            u8  htype = (u8)((hdr >> 16) & 0xFF);

            pci_print_device((u8)bus, slot, 0, &dev);
            total++;

            if (!(htype & 0x80)) continue;

            for (u8 func = 1; func < 8; func++) {
                if (!pci_get_device((u8)bus, slot, func, &dev)) continue;
                pci_print_device((u8)bus, slot, func, &dev);
                total++;
            }
        }
    }

    if (total == 0)
        printk(T, "[PCI] No devices found\n");
    else
        printk(T, "[PCI] %d device(s) found\n", total);
}

/* -----------------------------------------------------------------------------
 * scan_rtc_devices
 * ----------------------------------------------------------------------------- */
void scan_rtc_devices(void)
{
    printk(T, "[RTC] Scanning PCI for RTC/timer devices...\n");
    int found = 0;

    for (int bus = 0; bus < 16; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            pci_device_t dev;
            if (!pci_get_device((u8)bus, slot, 0, &dev)) continue;

            u32 hdr      = pciread((u8)bus, slot, 0, 0x0C);
            u8  htype    = (u8)((hdr >> 16) & 0xFF);
            u8  max_func = (htype & 0x80) ? 8 : 1;

            for (u8 func = 0; func < max_func; func++) {
                if (!pci_get_device((u8)bus, slot, func, &dev)) continue;

                bool is_rtc  = (dev.class == 0x08 &&
                                (dev.subclass == 0x02 || dev.subclass == 0x03));
                bool is_lpc  = (dev.vendor_id == 0x8086 &&
                                dev.class == 0x06 && dev.subclass == 0x01);
                bool is_piix = (dev.vendor_id == 0x8086 &&
                                (dev.device_id == 0x7000 ||
                                 dev.device_id == 0x7010 ||
                                 dev.device_id == 0x7113));

                if (!is_rtc && !is_lpc && !is_piix) continue;

                const char *label =
                    is_rtc  ? (dev.subclass == 0x03 ? "RTC Controller" : "Timer") :
                    is_piix ? "PIIX South-Bridge (has RTC)" :
                              "ICH LPC Bridge (has RTC)";

                printk(T, "[RTC] [%02x:%02x.%x] %04x:%04x  %s\n",
                       bus, (u32)slot, (u32)func,
                       (u32)dev.vendor_id, (u32)dev.device_id, label);
                pci_print_bars((u8)bus, slot, func, 0);
                found++;
            }
        }
    }

    if (!found)
        printk(T, "[RTC] No PCI RTC found (CMOS RTC at 0x70/0x71 is ISA)\n");
    else
        printk(T, "[RTC] Found %d RTC device(s)\n", found);
}

/* -----------------------------------------------------------------------------
 * scan_usb_controllers
 * ----------------------------------------------------------------------------- */
void scan_usb_controllers(void)
{
    printk(T, "[USB] Scanning for USB controllers...\n");
    int found = 0;

    for (int bus = 0; bus < 16; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            pci_device_t dev;
            if (!pci_get_device((u8)bus, slot, 0, &dev)) continue;

            u32 hdr      = pciread((u8)bus, slot, 0, 0x0C);
            u8  htype    = (u8)((hdr >> 16) & 0xFF);
            u8  max_func = (htype & 0x80) ? 8 : 1;

            for (u8 func = 0; func < max_func; func++) {
                if (!pci_get_device((u8)bus, slot, func, &dev)) continue;
                if (dev.class != 0x0C || dev.subclass != 0x03) continue;

                printk(T, "[USB] [%02x:%02x.%x] %04x:%04x  %s Controller\n",
                       bus, (u32)slot, (u32)func,
                       (u32)dev.vendor_id, (u32)dev.device_id,
                       usb_type(dev.prog_if));
                pci_print_bars((u8)bus, slot, func, 0);
                found++;
            }
        }
    }

    printk(T, "[USB] Found %d USB controller(s)\n", found);
}

/* -----------------------------------------------------------------------------
 * pci_usb_kbd_present
 * ----------------------------------------------------------------------------- */
u8 pci_usb_kbd_present(void)
{
    for (int bus = 0; bus < 16; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            pci_device_t dev;
            if (!pci_get_device((u8)bus, slot, 0, &dev)) continue;
            if (dev.class == 0x0C && dev.subclass == 0x03) return 1;
        }
    }
    return 0;
}
