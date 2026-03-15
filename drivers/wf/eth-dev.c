#include "ark/printk.h"
#include "ark/pci.h"

// =============================================================================
// drivers/wf/eth-dev.c — Ethernet Controller Detection for ARK kernel
// =============================================================================

// -----------------------------------------------------------------------------
// Driver lookup table — Vendor ID + Device ID → driver/chip name
// -----------------------------------------------------------------------------

typedef struct {
    u16         vendor_id;
    u16         device_id;
    const char *driver_name;
    const char *chip_name;
} eth_driver_entry_t;

static const eth_driver_entry_t eth_driver_table[] = {
    // Intel
    { 0x8086, 0x100E, "e1000",      "Intel 82540EM (QEMU e1000)"   },
    { 0x8086, 0x100F, "e1000",      "Intel 82545EM"                },
    { 0x8086, 0x10D3, "e1000e",     "Intel 82574L"                 },
    { 0x8086, 0x1533, "igb",        "Intel i210"                   },
    // Realtek
    { 0x10EC, 0x8139, "rtl8139",    "Realtek RTL8139"              },
    { 0x10EC, 0x8168, "r8169",      "Realtek RTL8111/8168"         },
    { 0x10EC, 0x8169, "r8169",      "Realtek RTL8169 Gigabit"      },
    // VirtIO
    { 0x1AF4, 0x1000, "virtio_net", "VirtIO Network (QEMU/KVM)"    },
    { 0x1AF4, 0x1041, "virtio_net", "VirtIO Network (modern)"      },
    // Intel extended
    { 0x8086, 0x1502, "e1000e",     "Intel 82579LM (PCH)"          },
    { 0x8086, 0x1503, "e1000e",     "Intel 82579V (PCH)"           },
    { 0x8086, 0x15B8, "e1000e",     "Intel I219-V"                 },
    // AMD PCnet
    { 0x1022, 0x2000, "pcnet",      "AMD PCnet-PCI II"             },
    { 0x1022, 0x2001, "pcnet",      "AMD PCnet-PCI III"            },
    // Broadcom
    { 0x14E4, 0x1657, "tg3",        "Broadcom BCM5719"             },
    { 0x14E4, 0x16B4, "tg3",        "Broadcom BCM57765"            },
    // VMware
    { 0x15AD, 0x07B0, "vmxnet3",    "VMware VMXNET3"               },
    { 0x15AD, 0x0720, "vmxnet2",    "VMware VMXNET2"               },
    // Realtek extended
    { 0x10EC, 0x8136, "r8169",      "Realtek RTL8101/8102E"        },
    { 0x10EC, 0x8161, "r8169",      "Realtek RTL8111 Gigabit"      },
    // Sentinel — must stay last
    { 0x0000, 0x0000, NULL,         NULL                           }
};

// -----------------------------------------------------------------------------
// Ethernet device record — defined RIGHT HERE, no external header needed
// -----------------------------------------------------------------------------

typedef struct {
    u8          bus;
    u8          slot;
    u8          func;
    u16         vendor_id;
    u16         device_id;
    u8          class_code;
    u8          subclass;
    u8          prog_if;
    u32         bar0;           // NIC register base (flag bits stripped)
    const char *driver_name;
    const char *chip_name;
} eth_dev_t;

#define MAX_ETH_DEVICES 16

static eth_dev_t eth_devs[MAX_ETH_DEVICES];
static u8        eth_dev_count = 0;

// -----------------------------------------------------------------------------
// Internal: look up driver by vendor + device ID
// -----------------------------------------------------------------------------

static const eth_driver_entry_t *eth_lookup(u16 vendor, u16 device)
{
    for (int i = 0; eth_driver_table[i].driver_name != NULL; i++) {
        if (eth_driver_table[i].vendor_id == vendor &&
            eth_driver_table[i].device_id == device)
            return &eth_driver_table[i];
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// detect_eth() — scan PCI bus for Ethernet controllers
//   Class 0x02, Subclass 0x00 = Ethernet Controller (PCI spec)
//   Returns number of NICs found.
// -----------------------------------------------------------------------------

u8 detect_eth(void)
{
    eth_dev_count = 0;
    pci_device_t dev;

    for_each_pci_device(dev) {

        // Only want Ethernet controllers
        if (dev.class    != 0x02) continue;
        if (dev.subclass != 0x00) continue;
        if (eth_dev_count >= MAX_ETH_DEVICES) break;

        eth_dev_t *e  = &eth_devs[eth_dev_count];

        e->bus        = dev.bus;
        e->slot       = dev.slot;
        e->func       = dev.func;
        e->vendor_id  = dev.vendor_id;
        e->device_id  = dev.device_id;
        e->class_code = dev.class;
        e->subclass   = dev.subclass;
        e->prog_if    = dev.prog_if;

        // BAR0 = NIC's own register base address
        // bit 0 == 0 → MMIO BAR (mask lower 4 bits)
        // bit 0 == 1 → I/O  BAR (mask lower 2 bits)
        u32 bar0 = pci_read_bar(dev.bus, dev.slot, dev.func, 0);
        e->bar0  = (bar0 & 0x1) ? (bar0 & 0xFFFFFFFC)
                                 : (bar0 & 0xFFFFFFF0);

        const eth_driver_entry_t *drv = eth_lookup(dev.vendor_id, dev.device_id);
        e->driver_name = drv ? drv->driver_name : "unknown";
        e->chip_name   = drv ? drv->chip_name   : "Unknown Ethernet Controller";

        eth_dev_count++;
    }

    return eth_dev_count;
}

// -----------------------------------------------------------------------------
// print_eth() — detect and print all Ethernet controllers
// -----------------------------------------------------------------------------

void print_eth_devices(void)
{
    printk(T,"[ETH] Scanning PCI for Ethernet controllers...\n");

    u8 count = detect_eth();

    if (count == 0) {
        printk(T,"[ETH] No Ethernet controllers found.\n");
        printk(T,"[ETH] Make sure scanAll() works first (PCI must be functional).\n");
        return;
    }

    printk(T,"[ETH] Found %u Ethernet controller(s):\n", (u32)count);

    for (u8 i = 0; i < count; i++) {
        eth_dev_t *e = &eth_devs[i];

        printk("\n[ETH] #%u  %s\n",               (u32)i, e->chip_name);
        printk("  PCI    : %02x:%02x.%x\n",        (u32)e->bus,
                                                    (u32)e->slot,
                                                    (u32)e->func);
        printk("  IDs    : vendor=%04x  device=%04x\n",
                                                    (u32)e->vendor_id,
                                                    (u32)e->device_id);
        printk("  Class  : %02x.%02x.%02x\n",      (u32)e->class_code,
                                                    (u32)e->subclass,
                                                    (u32)e->prog_if);
        printk("  BAR0   : 0x%08x\n",              e->bar0);
        printk("  Driver : %s\n",                  e->driver_name);
    }

    printk(T,"[ETH] Done.\n");
}
// -----------------------------------------------------------------------------
// pci_e1000_present() — returns 1 if any Intel e1000 NIC was found
// Used by init_api.c to fill has_e1000 flag
// -----------------------------------------------------------------------------

u8 pci_e1000_present(void)
{
    if (eth_dev_count == 0)
        detect_eth();

    for (u8 i = 0; i < eth_dev_count; i++) {
        if (eth_devs[i].vendor_id == 0x8086 &&
           (eth_devs[i].device_id == 0x100E ||
            eth_devs[i].device_id == 0x100F ||
            eth_devs[i].device_id == 0x10D3 ||
            eth_devs[i].device_id == 0x1533))
            return 1;
    }
    return 0;
}
// -----------------------------------------------------------------------------
// eth_get_count() / eth_get_dev_ids() — let net.c reuse detect_eth() results
// so the PCI bus is only walked once.
// -----------------------------------------------------------------------------

u8 eth_get_count(void) {
    if (eth_dev_count == 0) detect_eth();
    return eth_dev_count;
}

// Returns 1 and fills vid/did/bus/slot/func if index is valid, else 0.
u8 eth_get_dev(u8 idx, u16 *vid, u16 *did, u8 *bus, u8 *slot, u8 *func) {
    if (idx >= eth_dev_count) return 0;
    if (vid)  *vid  = eth_devs[idx].vendor_id;
    if (did)  *did  = eth_devs[idx].device_id;
    if (bus)  *bus  = eth_devs[idx].bus;
    if (slot) *slot = eth_devs[idx].slot;
    if (func) *func = eth_devs[idx].func;
    return 1;
}
