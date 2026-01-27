#include "ark/usb.h"
#include "ark/pci.h"
#include "ark/mmio.h"
#include "ark/printk.h"
#include "ehci.h"
#include <stdint.h>

#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_EHCI  0x20
#define EHCI_REG_SIZE 0x1000

typedef volatile uint32_t vuint32_t;


typedef struct {
    uint8_t caplength;      
    uint8_t reserved;       
    uint16_t hciversion;    
    uint32_t hcsparams;     
    uint32_t hccparams;     
    uint32_t hcspportroute; 
} ehci_cap_regs_t;

typedef struct {
    vuint32_t usbcmd;       
    vuint32_t usbsts;       
    vuint32_t usbintr;      
    vuint32_t frindex;      
    vuint32_t ctrldssegment;
    vuint32_t periodiclistbase; 
    vuint32_t asynclistaddr;
    uint32_t  reserved[9];  
    vuint32_t configflag;   
    vuint32_t portsc[8];    
} ehci_op_regs_t;

typedef struct {
    ehci_cap_regs_t *cap;
    ehci_op_regs_t  *op;
    int port_count;
} ehci_controller_t;

static ehci_controller_t ehci_ctrlr = {0};


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


