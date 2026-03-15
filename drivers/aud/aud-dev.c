/* ============================================================
 * audio_scanner.c  —  Baremetal Audio Device Scanner
 *
 * Scans the PCI bus (brute-force enumerate bus/slot/func)
 * for known audio hardware using raw port I/O.
 *
 * Uses ark/printk.h — all logging via printk(T, "...") style. 
 * Usage in your kernel init:
 *
 *   audio_scan_result_t scan;
 *   audio_scanner_run(&scan);
 *   audio_scanner_print_report(&scan);
 *
 *   // Activate first supported device:
 *   audio_scanner_use_device(&scan, 0);
 *
 * ============================================================ */

#include "aud/aud-dev.h"
#include "ark/printk.h"
#include "ark/types.h"


/* ── Port I/O (baremetal x86 — replace with your arch's I/O API) ─ */

static inline void outl(u16 port, u32 val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ── PCI config space read ──────────────────────────────────────── */

static u32 pci_read(u8 bus, u8 slot,
                          u8 func, u8 offset) {
    u32 addr = (u32)(
        (1U              << 31) |
        ((u32)bus   << 16) |
        ((u32)slot  << 11) |
        ((u32)func  <<  8) |
        (offset & 0xFC)
    );
    outl(PCI_CONFIG_ADDRESS, addr);
    u32 data = inl(PCI_CONFIG_DATA);
    return (data >> ((offset & 2) * 8));
}

static u16 pci_read16(u8 bus, u8 slot,
                            u8 func, u8 offset) {
    return (u16)(pci_read(bus, slot, func, offset) & 0xFFFF);
}

static u8 pci_read8(u8 bus, u8 slot,
                          u8 func, u8 offset) {
    return (u8)(pci_read(bus, slot, func, offset) & 0xFF);
}

/* ── Known audio PCI device table ───────────────────────────────── */

typedef struct {
    u16        vendor;
    u16        device;
    const char*     name;
    driver_status_t driver;
    const char*     driver_name;
} pci_audio_id_t;

static const pci_audio_id_t known_audio_devices[] = {

    /* ── Intel HD Audio ────────────────────────────────────────── */
    { 0x8086, 0x2668, "Intel ICH6 HD Audio",           DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x27D8, "Intel ICH7 HD Audio",           DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x269A, "Intel ESB2 HD Audio",           DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x284B, "Intel ICH8 HD Audio",           DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x293E, "Intel ICH9 HD Audio",           DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x3A3E, "Intel ICH10 HD Audio",          DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x1C20, "Intel 6 Series HD Audio",       DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x1E20, "Intel 7 Series HD Audio",       DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x8C20, "Intel 8 Series HD Audio",       DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x9C20, "Intel Wildcat Point HD Audio",  DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0xA170, "Intel 100 Series HD Audio",     DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0x9D70, "Intel Sunrise Point HD Audio",  DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0xA2F0, "Intel 200 Series HD Audio",     DRIVER_STATUS_HDA,     "hda_intel"   },
    { 0x8086, 0xA348, "Intel 300 Series HD Audio",     DRIVER_STATUS_HDA,     "hda_intel"   },

    /* ── Intel AC97 / ICH ──────────────────────────────────────── */
    { 0x8086, 0x2415, "Intel ICH AC97 Audio",          DRIVER_STATUS_AC97,    "intel_ac97"  },
    { 0x8086, 0x2425, "Intel ICH2 AC97 Audio",         DRIVER_STATUS_AC97,    "intel_ac97"  },
    { 0x8086, 0x2445, "Intel ICH2 AC97 Modem",         DRIVER_STATUS_AC97,    "intel_ac97"  },
    { 0x8086, 0x2485, "Intel ICH3 AC97 Audio",         DRIVER_STATUS_AC97,    "intel_ac97"  },
    { 0x8086, 0x24C5, "Intel ICH4 AC97 Audio",         DRIVER_STATUS_ICH,     "intel_ich"   },
    { 0x8086, 0x24D5, "Intel ICH5 AC97 Audio",         DRIVER_STATUS_ICH,     "intel_ich"   },
    { 0x8086, 0x25A6, "Intel ESB AC97 Audio",          DRIVER_STATUS_ICH,     "intel_ich"   },
    { 0x8086, 0x266E, "Intel ICH6 AC97 Audio",         DRIVER_STATUS_ICH,     "intel_ich"   },

    /* ── Realtek HDA codecs ─────────────────────────────────────── */
    { 0x10EC, 0x0880, "Realtek ALC880 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0882, "Realtek ALC882 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0883, "Realtek ALC883 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0885, "Realtek ALC885 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0887, "Realtek ALC887 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0888, "Realtek ALC888 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0892, "Realtek ALC892 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0897, "Realtek ALC897 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0221, "Realtek ALC221 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0233, "Realtek ALC233 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0235, "Realtek ALC235 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0255, "Realtek ALC255 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0256, "Realtek ALC256 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0269, "Realtek ALC269 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0295, "Realtek ALC295 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },
    { 0x10EC, 0x0298, "Realtek ALC298 HD Audio",       DRIVER_STATUS_REALTEK, "realtek_hda" },

    /* ── Creative / EMU ────────────────────────────────────────── */
    { 0x1102, 0x0002, "Creative EMU10K1 (SB Live!)",   DRIVER_STATUS_CREATIVE,"emu10k"      },
    { 0x1102, 0x0004, "Creative EMU10K2 (Audigy)",     DRIVER_STATUS_CREATIVE,"emu10k"      },
    { 0x1102, 0x0008, "Creative EMU10K3 (Audigy2)",    DRIVER_STATUS_CREATIVE,"emu10k"      },
    { 0x1102, 0x000B, "Creative EMU20K1 (X-Fi)",       DRIVER_STATUS_CREATIVE,"emu20k"      },
    { 0x1102, 0x0009, "Creative SB Audigy4",           DRIVER_STATUS_CREATIVE,"emu10k"      },

    /* ── Sound Blaster 16 ──────────────────────────────────────── */
    { 0x1102, 0x0007, "Creative Sound Blaster 16/AWE", DRIVER_STATUS_SB16,    "sb16"        },

    /* ── Ensoniq ───────────────────────────────────────────────── */
    { 0x1274, 0x5000, "Ensoniq ES1370 AudioPCI",       DRIVER_STATUS_ENSONIQ, "ensoniq"     },
    { 0x1274, 0x1371, "Ensoniq ES1371 AudioPCI97",     DRIVER_STATUS_ENSONIQ, "ensoniq"     },
    { 0x1274, 0x5880, "Ensoniq CT5880 AudioPCI",       DRIVER_STATUS_ENSONIQ, "ensoniq"     },

    /* ── C-Media ───────────────────────────────────────────────── */
    { 0x13F6, 0x0100, "C-Media CMI8338A",              DRIVER_STATUS_CMEDIA,  "cmedia"      },
    { 0x13F6, 0x0101, "C-Media CMI8338B",              DRIVER_STATUS_CMEDIA,  "cmedia"      },
    { 0x13F6, 0x0111, "C-Media CMI8738",               DRIVER_STATUS_CMEDIA,  "cmedia"      },
    { 0x13F6, 0x011C, "C-Media CMI8738-MX",            DRIVER_STATUS_CMEDIA,  "cmedia"      },

    /* ── VIA ───────────────────────────────────────────────────── */
    { 0x1106, 0x3058, "VIA VT82C686 AC97 Audio",       DRIVER_STATUS_VIA,     "via_audio"   },
    { 0x1106, 0x3059, "VIA VT8233/A AC97 Audio",       DRIVER_STATUS_VIA,     "via_audio"   },
    { 0x1106, 0x7122, "VIA VT8251/VX800 HD Audio",     DRIVER_STATUS_HDA,     "hda_via"     },

    /* ── AMD ───────────────────────────────────────────────────── */
    { 0x1022, 0x780D, "AMD FCH Azalia HD Audio",       DRIVER_STATUS_HDA,     "hda_amd"     },
    { 0x1022, 0x157A, "AMD Kabini HD Audio",           DRIVER_STATUS_HDA,     "hda_amd"     },
    { 0x1022, 0x15E3, "AMD Raven/Raven2 HD Audio",     DRIVER_STATUS_HDA,     "hda_amd"     },
    { 0x1022, 0x1457, "AMD Starship HD Audio",         DRIVER_STATUS_HDA,     "hda_amd"     },

    /* ── NVIDIA ────────────────────────────────────────────────── */
    { 0x10DE, 0x0059, "NVIDIA CK804 AC97 Audio",       DRIVER_STATUS_AC97,    "nvidia_ac97" },
    { 0x10DE, 0x026C, "NVIDIA MCP51 HD Audio",         DRIVER_STATUS_HDA,     "hda_nvidia"  },
    { 0x10DE, 0x0774, "NVIDIA MCP72 HD Audio",         DRIVER_STATUS_HDA,     "hda_nvidia"  },
    { 0x10DE, 0x0AC0, "NVIDIA MCP79 HD Audio",         DRIVER_STATUS_HDA,     "hda_nvidia"  },
    { 0x10DE, 0x0E1B, "NVIDIA GK104 HDMI Audio",       DRIVER_STATUS_HDA,     "hda_nvidia"  },

    /* sentinel — must be last */
    { 0x0000, 0x0000, NULL, DRIVER_STATUS_NONE, NULL }
};

/* ── Internal helpers ───────────────────────────────────────────── */

static const pci_audio_id_t* match_device(u16 vendor, u16 device) {
    for (int i = 0; known_audio_devices[i].name != NULL; i++) {
        if (known_audio_devices[i].vendor == vendor &&
            known_audio_devices[i].device == device)
            return &known_audio_devices[i];
    }
    return NULL;
}

static bool is_audio_class(u8 cls, u8 sub) {
    if (cls != PCI_CLASS_MULTIMEDIA) return false;
    return (sub == PCI_SUBCLASS_AUDIO ||
            sub == PCI_SUBCLASS_HDA   ||
            sub == PCI_SUBCLASS_MULTIMEDIA_OTHER);
}

static const char* driver_status_to_str(driver_status_t d) {
    switch (d) {
        case DRIVER_STATUS_HDA:       return "HDA (Intel HD Audio)";
        case DRIVER_STATUS_AC97:      return "AC97";
        case DRIVER_STATUS_SB16:      return "Sound Blaster 16";
        case DRIVER_STATUS_ICH:       return "Intel ICH Audio";
        case DRIVER_STATUS_ENSONIQ:   return "Ensoniq AudioPCI";
        case DRIVER_STATUS_CMEDIA:    return "C-Media";
        case DRIVER_STATUS_VIA:       return "VIA Audio";
        case DRIVER_STATUS_REALTEK:   return "Realtek HDA Codec";
        case DRIVER_STATUS_CREATIVE:  return "Creative EMU";
        case DRIVER_STATUS_USB_AUDIO: return "USB Audio Class";
        case DRIVER_STATUS_UNKNOWN:
        case DRIVER_STATUS_NONE:
        default:                      return "No relevant driver found";
    }
}

static void ascan_memset(void* ptr, u8 val, u32 size) {
    u8* p = (u8*)ptr;
    for (u32 i = 0; i < size; i++) p[i] = val;
}

/* ── Public: audio_scanner_run() ────────────────────────────────── */

int audio_scanner_run(audio_scan_result_t* result) {
    if (!result) return -1;

    ascan_memset(result, 0, sizeof(audio_scan_result_t));

    printk(T, "audio_scanner: Audio Device Scan Start \n");

    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            for (u8 func = 0; func < 8; func++) {

                u16 vendor = (u16)(
                    pci_read(bus, slot, func, PCI_VENDOR_ID) & 0xFFFF);

                if (vendor == 0xFFFF || vendor == 0x0000)
                    continue;

                u16 device_id   = pci_read16(bus, slot, func, PCI_DEVICE_ID);
                u32 class_rev   = pci_read(bus, slot, func, PCI_CLASS_REVISION);
                u8  pci_class   = (u8)((class_rev >> 24) & 0xFF);
                u8  pci_subclass= (u8)((class_rev >> 16) & 0xFF);

                bool by_class = is_audio_class(pci_class, pci_subclass);
                const pci_audio_id_t* match = match_device(vendor, device_id);

                if (!by_class && !match)
                    continue;

                if (result->count >= AUDIO_SCANNER_MAX_DEVICES) {
                    printk(T, "audio_scanner: device table full, skipping %02x:%02x.%x\n",
                           bus, slot, func);
                    continue;
                }

                audio_device_t* dev  = &result->devices[result->count];
                dev->bus             = (u8)bus;
                dev->slot            = slot;
                dev->func            = func;
                dev->vendor_id       = vendor;
                dev->device_id       = device_id;
                dev->pci_class       = pci_class;
                dev->pci_subclass    = pci_subclass;
                dev->bar0            = pci_read(bus, slot, func, PCI_BAR0) & 0xFFFFFFF0;
                dev->in_use          = false;

                if (match) {
                    dev->driver      = match->driver;
                    dev->driver_name = match->driver_name;
                    dev->device_name = match->name;
                    result->supported++;
                } else {
                    dev->driver      = DRIVER_STATUS_UNKNOWN;
                    dev->driver_name = NULL;
                    dev->device_name = "Unknown Audio Device";
                    result->unsupported++;
                }

                result->count++;
            }
        }
    }

    printk(T, "audio_scanner: Scan complete. Found %u device(s) [%u supported, %u unsupported]\n",
           result->count, result->supported, result->unsupported);

    return (int)result->count;
}

/* ── Public: audio_scanner_use_device() ────────────────────────── */

bool audio_scanner_use_device(audio_scan_result_t* result, u32 index) {
    if (!result || index >= result->count) {
        printk(T, "audio_scanner: use_device: invalid index %u\n", index);
        return false;
    }

    audio_device_t* dev = &result->devices[index];

    if (dev->driver == DRIVER_STATUS_UNKNOWN ||
        dev->driver == DRIVER_STATUS_NONE) {
        printk(T, "audio_scanner: [%02x:%02x.%x] %04x:%04x — No relevant driver found. Cannot activate.\n",
               dev->bus, dev->slot, dev->func,
               dev->vendor_id, dev->device_id);
        return false;
    }

    dev->in_use = true;

    printk(T, "audio_scanner: [%02x:%02x.%x] Activating '%s' using driver '%s' (BAR0=0x%08x)\n",
           dev->bus, dev->slot, dev->func,
           dev->device_name, dev->driver_name, dev->bar0);

    /*
     * Call your driver probe here. Example:
     *   if (dev->driver == DRIVER_STATUS_HDA)
     *       hda_init(dev->bar0);
     *   else if (dev->driver == DRIVER_STATUS_AC97)
     *       ac97_init(dev->bar0);
     */

    return true;
}

/* ── Public: audio_scanner_print_report() ──────────────────────── */

void audio_scanner_print_report(const audio_scan_result_t* result) {
    if (!result) return;

    printk(T, "audio_scanner: Audio Scan Report\n");

    if (result->count == 0) {
        printk(T, "audio_scanner: No audio hardware detected on PCI bus.\n");
        return;
    }

    for (u32 i = 0; i < result->count; i++) {
        const audio_device_t* dev = &result->devices[i];

        printk(T, "audio_scanner: [%u] Bus=%02x Slot=%02x Func=%x\n",
               i, dev->bus, dev->slot, dev->func);

        printk(T, "audio_scanner:     Device  : %s\n",
               dev->device_name ? dev->device_name : "Unknown");

        printk(T, "audio_scanner:     VendorID: 0x%04x  DeviceID: 0x%04x\n",
               dev->vendor_id, dev->device_id);

        printk(T, "audio_scanner:     Class   : 0x%02x  Subclass: 0x%02x\n",
               dev->pci_class, dev->pci_subclass);

        printk(T, "audio_scanner:     BAR0    : 0x%08x\n",
               dev->bar0);

        printk(T, "audio_scanner:     Driver  : %s\n",
               driver_status_to_str(dev->driver));

        printk(T, "audio_scanner:     In Use  : %s\n",
               dev->in_use ? "YES" : "NO");

        printk(T, "audio_scanner:     ----------------------------------------\n");
    }

    printk(T, "audio_scanner: Total: %u  Supported: %u  No driver: %u\n",
           result->count, result->supported, result->unsupported);

    printk(T, "audio_scanner\n");
}