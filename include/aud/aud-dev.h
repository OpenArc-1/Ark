#ifndef AUDIO_SCANNER_H
#define AUDIO_SCANNER_H

/* ============================================================
 * audio_scanner.h  —  Baremetal Audio Device Scanner
 *
 * Scans PCI bus for known audio hardware.
 * Uses ark/printk.h for logging.
 * Provides use_device() option to activate a found device.
 * Reports "No relevant driver found" for unknown hardware.
 * ============================================================ */

#include <ark/printk.h>
#include <stdint.h>
#include <stdbool.h>

/* ── PCI port I/O addresses (standard x86 config mechanism #1) ── */
#define PCI_CONFIG_ADDRESS   0xCF8
#define PCI_CONFIG_DATA      0xCFC

/* ── PCI config space offsets ─────────────────────────────────── */
#define PCI_VENDOR_ID        0x00
#define PCI_DEVICE_ID        0x02
#define PCI_CLASS_REVISION   0x08
#define PCI_HEADER_TYPE      0x0E
#define PCI_SUBSYSTEM_VENDOR 0x2C
#define PCI_SUBSYSTEM_ID     0x2E
#define PCI_BAR0             0x10

/* ── PCI class codes for audio ────────────────────────────────── */
#define PCI_CLASS_MULTIMEDIA          0x04
#define PCI_SUBCLASS_AUDIO            0x01   /* Legacy audio controller  */
#define PCI_SUBCLASS_HDA              0x03   /* Intel HD Audio           */
#define PCI_SUBCLASS_MULTIMEDIA_OTHER 0x80

/* ── Max devices the scanner will track ──────────────────────── */
#define AUDIO_SCANNER_MAX_DEVICES     32

/* ── Driver status for a found device ────────────────────────── */
typedef enum {
    DRIVER_STATUS_NONE      = 0,   /* unknown / unsupported device  */
    DRIVER_STATUS_HDA       = 1,   /* Intel HD Audio (HDA)          */
    DRIVER_STATUS_AC97      = 2,   /* AC'97 audio                   */
    DRIVER_STATUS_SB16      = 3,   /* Sound Blaster 16 / compat     */
    DRIVER_STATUS_ICH       = 4,   /* Intel ICH audio               */
    DRIVER_STATUS_ENSONIQ   = 5,   /* Ensoniq ES1370/ES1371         */
    DRIVER_STATUS_CMEDIA    = 6,   /* C-Media CMI8x38               */
    DRIVER_STATUS_VIA       = 7,   /* VIA VT82xx audio              */
    DRIVER_STATUS_REALTEK   = 8,   /* Realtek HDA codec             */
    DRIVER_STATUS_CREATIVE  = 9,   /* Creative / EMU10K             */
    DRIVER_STATUS_USB_AUDIO = 10,  /* USB Audio Class               */
    DRIVER_STATUS_UNKNOWN   = 0xFF /* device found but no driver    */
} driver_status_t;

/* ── A single scanned audio device entry ─────────────────────── */
typedef struct {
    uint8_t         bus;
    uint8_t         slot;
    uint8_t         func;
    uint16_t        vendor_id;
    uint16_t        device_id;
    uint8_t         pci_class;
    uint8_t         pci_subclass;
    uint32_t        bar0;           /* Base Address Register 0      */
    driver_status_t driver;
    bool            in_use;         /* activated via use_device()   */
    const char*     driver_name;
    const char*     device_name;
} audio_device_t;

/* ── Scanner result ───────────────────────────────────────────── */
typedef struct {
    audio_device_t  devices[AUDIO_SCANNER_MAX_DEVICES];
    uint32_t        count;          /* total devices found          */
    uint32_t        supported;      /* devices with known drivers   */
    uint32_t        unsupported;    /* "no relevant driver found"   */
} audio_scan_result_t;

/* ── Public API ───────────────────────────────────────────────── */

/*
 * audio_scanner_run()
 *   Scans PCI bus, fills result, prints report via ark_printk.
 *   Returns number of audio devices found (0 if none).
 */
int audio_scanner_run(audio_scan_result_t* result);

/*
 * audio_scanner_use_device(result, index)
 *   Activates the device at [index] in the result list.
 *   Sets device->in_use = true and initialises its BAR.
 *   Returns false if index invalid or driver unsupported.
 */
bool audio_scanner_use_device(audio_scan_result_t* result, uint32_t index);

/*
 * audio_scanner_print_report(result)
 *   Pretty-prints the full scan report via ark_printk.
 */
void audio_scanner_print_report(const audio_scan_result_t* result);

#endif /* AUDIO_SCANNER_H */