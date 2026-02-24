#include "ark/usb.h"
#include "ark/pci.h"
#include "ark/mmio.h"
#include "ark/printk.h"
#include "ark/types.h"

#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_EHCI  0x20
#define EHCI_REG_SIZE    0x1000

typedef volatile u32 vuint32_t;

typedef struct {
    u8 caplength;
    u8 reserved;
    u16 hciversion;
    u32 hcsparams;
    u32 hccparams;
    u32 hcspportroute;
} ehci_cap_regs_t;

typedef struct {
    vuint32_t usbcmd;
    vuint32_t usbsts;
    vuint32_t usbintr;
    vuint32_t frindex;
    vuint32_t ctrldssegment;
    vuint32_t periodiclistbase;
    vuint32_t asynclistaddr;
    u32 reserved[9];
    vuint32_t configflag;
    vuint32_t portsc[8];
} ehci_op_regs_t;

typedef struct {
    ehci_cap_regs_t *cap;
    ehci_op_regs_t *op;
    int port_count;
    bool is_running;
} ehci_controller_t;

static ehci_controller_t ehci_ctrlr = {0};

// Mock delay function (replace with your kernel's actual sleep/delay)
static void delay_ms(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++) { __asm__("pause"); }
}

// --- NEW: Port Reset and Speed Detection ---
static void ehci_scan_ports(ehci_controller_t *ctrl) {
    printk(T,"EHCI: Scanning %d ports...\n", ctrl->port_count);

    for (int i = 0; i < ctrl->port_count; i++) {
        u32 status = ctrl->op->portsc[i];
        
        if (status & 0x01) { // Bit 0: Connected
            printk(T,"EHCI: Device connected on port %d. Initiating reset...\n", i);

            // 1. Reset the port (Set bit 8)
            ctrl->op->portsc[i] = (status & ~0x2A) | (1 << 8);
            delay_ms(50); // USB spec requires at least 50ms reset

            // 2. Clear reset bit
            ctrl->op->portsc[i] &= ~(1 << 8);
            delay_ms(10); // Wait for port to recover

            // 3. Read status again to check speed
            status = ctrl->op->portsc[i];
            
            // Check if the port actually enabled itself (Bit 2)
            if (status & (1 << 2)) {
                printk(T,"EHCI: Port %d enabled! High-Speed device attached.\n", i);
                // Here is where you would normally set up the device address
            } else {
                // IMPORTANT: If it didn't enable, it's a Low/Full speed device (like a keyboard!)
                // We MUST set the "Port Owner" bit (Bit 13) to hand it to the UHCI companion.
                printk(T,"EHCI: Port %d is Low/Full speed. Handoff to Companion Controller.\n", i);
                ctrl->op->portsc[i] |= (1 << 13);
            }
        }
    }
}

// --- NEW: Controller Start Sequence ---
static void ehci_init(u32 bar0) {
    u32 base = bar0 & ~0xF;
    if (!base) return;

    ehci_ctrlr.cap = (ehci_cap_regs_t*)mmio_map(base, EHCI_REG_SIZE);
    ehci_ctrlr.op = (ehci_op_regs_t*)((u8*)ehci_ctrlr.cap + ehci_ctrlr.cap->caplength);
    ehci_ctrlr.port_count = ehci_ctrlr.cap->hcsparams & 0x0F;

    if (ehci_ctrlr.port_count == 0 || ehci_ctrlr.port_count > 15) ehci_ctrlr.port_count = 1;

    // 1. Stop the controller just in case BIOS left it running
    ehci_ctrlr.op->usbcmd &= ~(1 << 0); // Clear RS (Run/Stop) bit
    while (ehci_ctrlr.op->usbcmd & (1 << 0)); // Wait for it to stop

    // 2. Reset the controller
    ehci_ctrlr.op->usbcmd |= (1 << 1); // Set HCRESET bit
    while (ehci_ctrlr.op->usbcmd & (1 << 1)); // Wait for reset to complete

    // 3. Route all ports to EHCI by default (Configure Flag)
    ehci_ctrlr.op->configflag = 1;
    delay_ms(5);

    // 4. Start the controller!
    ehci_ctrlr.op->usbcmd |= (1 << 0); // Set RS (Run) bit
    ehci_ctrlr.is_running = true;

    printk(T,"EHCI: Controller started with %d ports.\n", ehci_ctrlr.port_count);

    // Scan ports and reset devices
    ehci_scan_ports(&ehci_ctrlr);
}

// --- Your USB Init (Mostly unchanged) ---
void usb_init() {
    printk(T,"USB: Initializing USB subsystem...\n");
    pci_device_t dev;
    int controller_found = 0;

    for_each_pci_device(dev) {
        if (dev.class == PCI_CLASS_SERIAL && dev.subclass == PCI_SUBCLASS_USB) {
            controller_found++;
            
            if (dev.prog_if == PCI_PROGIF_EHCI) {
                printk(T,"USB: EHCI Controller found at %02x:%02x.%x\n", dev.bus, dev.slot, dev.func);
                u32 bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
                ehci_init(bar0);
            } 
            else if (dev.prog_if == 0x00 || dev.prog_if == 0x10) {
                // 0x00 is UHCI, 0x10 is OHCI. These are the "Companions"!
                printk(T,"USB: Companion Controller (UHCI/OHCI) found at %02x:%02x.%x\n", dev.bus, dev.slot, dev.func);
                // Future Step: Initialize UHCI to catch the keyboard you just handed off!
            }
        }
    }

    if (!controller_found) printk(T,"USB: No controllers found.\n");
}

// --- Polling Hook for input.c (UHCI/OHCI init is in input_init -> usb_kbd_init) ---
void usb_poll_all(void) {
    if (!ehci_ctrlr.is_running) return;
    usb_kbd_poll();
}