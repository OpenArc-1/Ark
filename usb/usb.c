#include "usb.h"
#include "../gen/pci.h"
#include "../mem/mmio.h"   // our phys → virt mapper
#include "ark/printk.h"

#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_EHCI  0x20
#define EHCI_REG_SIZE 0x1000

typedef volatile uint32_t vuint32_t;

typedef struct {
    vuint32_t usbcmd;     // 0x00
    vuint32_t usbsts;     // 0x04
    vuint32_t usbintr;    // 0x08
    vuint32_t frindex;    // 0x0C
    vuint32_t ctrldssegment;
    vuint32_t periodiclistbase;
    vuint32_t asynclistaddr;
    uint32_t  reserved[9];
    vuint32_t configflag; // 0x40
    vuint32_t portsc[8];  // 0x44+ ports
} ehci_op_regs_t;

static ehci_op_regs_t* ehci = 0;
static int port_count = 0;

static void ehci_scan_ports() {
    printk("EHCI: Scanning %d ports...\n", port_count);

    for (int i = 0; i < port_count; i++) {
        uint32_t status = ehci->portsc[i];

        if (status & 1) {  // Bit 0 = device connected
            printk("USB Device Connected on port %d\n", i);
        }
    }
}

static void ehci_init(uint32_t bar0) {
    uint32_t base = bar0 & ~0xF;

    printk("EHCI MMIO Base: %x\n", base);

    ehci = (ehci_op_regs_t*)mmio_map(base, EHCI_REG_SIZE);

    if (!ehci) {
        printk("EHCI map failed\n");
        return;
    }

    // Number of ports is in capability registers (offset 0x04)
    volatile uint8_t* cap = (uint8_t*)ehci;
    uint32_t hcsparams = *(volatile uint32_t*)(cap + 0x04);
    port_count = hcsparams & 0xF;

    printk("EHCI Ports: %d\n", port_count);

    ehci_scan_ports();
}

void usb_init() {
    printk("USB: Scanning PCI...\n");

    pci_device_t dev;
    int controller_found = 0;

    for_each_pci_device(dev){
        if (dev.class == PCI_CLASS_SERIAL &&
            dev.subclass == PCI_SUBCLASS_USB) {

            printk("USB Controller Found progIF=%x\n", dev.prog_if);
            controller_found++;

            if (dev.prog_if == PCI_PROGIF_EHCI) {
                printk("EHCI Controller detected\n");

                uint32_t bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
                ehci_init(bar0);
            }
        }
    }
    
    if (!controller_found) {
        printk("USB: No controllers found, simulating devices...\n");
        printk("USB Device Simulated on port 0: Keyboard\n");
        printk("USB Device Simulated on port 1: Mouse\n");
    }
}
