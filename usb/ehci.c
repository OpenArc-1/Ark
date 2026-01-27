
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
