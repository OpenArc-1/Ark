#include "ark/usb.h"
#include "ark/pci.h"
#include "ark/mmio.h"   // our phys → virt mapper
#include "ark/printk.h"

#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_EHCI  0x20
#define EHCI_REG_SIZE 0x1000

typedef volatile uint32_t vuint32_t;

// EHCI Capability Registers (at BAR0 base address)
typedef struct {
    uint8_t caplength;      // 0x00: Capability Registers Length
    uint8_t reserved;       // 0x01: reserved
    uint16_t hciversion;    // 0x02: Interface Version Number
    uint32_t hcsparams;     // 0x04: Structural Parameters
    uint32_t hccparams;     // 0x08: Capability Parameters
    uint32_t hcspportroute; // 0x0C: Companion Port Route Description
} ehci_cap_regs_t;

// EHCI Operational Registers (at BAR0 + caplength)
typedef struct {
    vuint32_t usbcmd;       // 0x00
    vuint32_t usbsts;       // 0x04
    vuint32_t usbintr;      // 0x08
    vuint32_t frindex;      // 0x0C
    vuint32_t ctrldssegment;// 0x10
    vuint32_t periodiclistbase; // 0x14
    vuint32_t asynclistaddr;// 0x18
    uint32_t  reserved[9];  // 0x1C-0x3F
    vuint32_t configflag;   // 0x40
    vuint32_t portsc[8];    // 0x44+ ports
} ehci_op_regs_t;

typedef struct {
    ehci_cap_regs_t *cap;
    ehci_op_regs_t *op;
    int port_count;
} ehci_controller_t;

static ehci_controller_t ehci_ctrlr = {0};

static void ehci_scan_ports(ehci_controller_t *ctrl) {
    printk("EHCI: Scanning %d ports...\n", ctrl->port_count);

    for (int i = 0; i < ctrl->port_count; i++) {
        uint32_t status = ctrl->op->portsc[i];
        uint32_t connect_status = status & 0x01;  // Bit 0: Current Connect Status
        uint32_t port_enabled = (status >> 2) & 0x01;  // Bit 2: Port Enabled/Disabled

        if (connect_status) {
            printk("EHCI: Device connected on port %d (enabled: %d)\n", i, port_enabled);
        }
    }
}

static void ehci_init(uint32_t bar0) {
    uint32_t base = bar0 & ~0xF;  // Mask off lower 4 bits for memory type

    if (!base) {
        printk("EHCI: Invalid BAR0 address\n");
        return;
    }

    printk("EHCI: Mapping MMIO base 0x%x\n", base);

    // Map capability registers
    ehci_ctrlr.cap = (ehci_cap_regs_t*)mmio_map(base, EHCI_REG_SIZE);

    if (!ehci_ctrlr.cap) {
        printk("EHCI: MMIO mapping failed\n");
        return;
    }

    // Read capability register length to find operational registers
    uint8_t caplength = ehci_ctrlr.cap->caplength;
    printk("EHCI: Capability register length: %d bytes\n", caplength);

    // Operational registers are at base + caplength
    ehci_ctrlr.op = (ehci_op_regs_t*)((uint8_t*)ehci_ctrlr.cap + caplength);

    // Extract port count from HCSPARAMS (bits 3:0)
    uint32_t hcsparams = ehci_ctrlr.cap->hcsparams;
    ehci_ctrlr.port_count = hcsparams & 0x0F;

    if (ehci_ctrlr.port_count == 0 || ehci_ctrlr.port_count > 15) {
        // Invalid port count, default to reasonable value
        printk("EHCI: Invalid port count %d, defaulting to 1\n", ehci_ctrlr.port_count);
        ehci_ctrlr.port_count = 1;
    }

    printk("EHCI: Controller initialized with %d ports\n", ehci_ctrlr.port_count);

    // Scan ports for connected devices
    ehci_scan_ports(&ehci_ctrlr);
}

void usb_init() {
    printk("USB: Initializing USB subsystem...\n");

    pci_device_t dev;
    int controller_found = 0;
    int devices_detected = 0;
    int max_retries = 5;
    int retry = 0;

    // Retry scanning up to 5 times
    while (retry < max_retries && controller_found == 0) {
        retry++;
        printk("USB: Scanning PCI buses for USB controllers (attempt %d/%d)...\n", retry, max_retries);

        // Scan all PCI devices for USB controllers
        for_each_pci_device(dev) {
            // Check if device is a USB controller
            if (dev.class == PCI_CLASS_SERIAL && dev.subclass == PCI_SUBCLASS_USB) {
                printk("USB: Controller found at %02x:%02x.%x (progIF: 0x%02x)\n",
                       dev.bus, dev.slot, dev.func, dev.prog_if);

                controller_found++;

                // Handle different USB controller types
                switch (dev.prog_if) {
                    case PCI_PROGIF_EHCI: {
                        printk("USB: Initializing EHCI controller...\n");
                        uint32_t bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
                        
                        if (bar0 != 0 && bar0 != 0xFFFFFFFFU) {
                            printk("USB: EHCI BAR0: 0x%x\n", bar0);
                            ehci_init(bar0);
                            
                            // Count connected devices
                            for (int p = 0; p < ehci_ctrlr.port_count; p++) {
                                if (ehci_ctrlr.op && (ehci_ctrlr.op->portsc[p] & 0x01)) {
                                    devices_detected++;
                                }
                            }
                        } else {
                            printk("USB: Invalid BAR0 address for EHCI controller (0x%x)\n", bar0);
                        }
                        break;
                    }
                    case 0x10:  // UHCI (Universal Host Controller Interface)
                        printk("USB: UHCI controller found at %02x:%02x.%x (not yet supported)\n",
                               dev.bus, dev.slot, dev.func);
                        break;
                    case 0x30:  // xHCI (eXtensible Host Controller Interface)
                        printk("USB: xHCI controller found at %02x:%02x.%x (not yet supported)\n",
                               dev.bus, dev.slot, dev.func);
                        break;
                    default:
                        printk("USB: Unknown USB controller type at %02x:%02x.%x (progIF: 0x%02x)\n",
                               dev.bus, dev.slot, dev.func, dev.prog_if);
                }
            }
        }

        // If no controller found yet and retries remain, wait before retrying
        if (controller_found == 0 && retry < max_retries) {
            printk("USB: No controller found on attempt %d, retrying...\n", retry);
        }
    }

    // Report summary
    if (controller_found == 0) {
        printk("USB: No controllers found after %d attempts (class=0x%02x, subclass=0x%02x)\n",
               max_retries, PCI_CLASS_SERIAL, PCI_SUBCLASS_USB);
        printk("USB: Check BIOS/UEFI settings to ensure USB is enabled\n");
        printk("USB: Run 'scanAll()' or 'scan_usb_controllers()' for debugging\n");
    } else {
        printk("USB: %d controller(s) found on attempt %d, %d device(s) detected\n",
               controller_found, retry, devices_detected);
    }
}
