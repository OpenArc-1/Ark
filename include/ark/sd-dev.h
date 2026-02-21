/**
 * disk_scanner.h - BIOS disk detection API
 */

#ifndef DISK_SCANNER_H
#define DISK_SCANNER_H

#include <stdint.h>

typedef struct {
    uint8_t  drive;           /* 0x80 = first HDD, 0x81 = second, etc.     */
    uint32_t sectors_lo;      /* low  32 bits of total LBA sector count    */
    uint32_t sectors_hi;      /* high 32 bits of total LBA sector count    */
    uint32_t total_mb;        /* Total size in megabytes                   */
    uint32_t total_gb_int;    /* GB integer part                           */
    uint32_t total_gb_frac;   /* GB fractional part (tenths: 0-9)          */
    uint8_t  method;          /* 1 = INT 13h extended, 2 = CHS fallback    */
    uint8_t  valid;           /* 1 if this entry contains valid data       */
} DiskInfo;

/**
 * scan_disk - Query BIOS for one drive's size
 * 
 * @param drive  BIOS drive number (0x80 = first HDD, 0x81 = second, etc.)
 * @param info   Pointer to DiskInfo structure to fill
 * @return       1 on success, 0 if drive not present
 */
int scan_disk(uint8_t drive, DiskInfo *info);

/**
 * scan_all_disk - Scan drives 0x80-0x8F, stop at first failure
 * 
 * @param results  Caller-allocated array of at least 16 DiskInfo entries
 * @return         Count of drives found
 */
int scan_all_disk(DiskInfo *results);

/**
 * print_disk_info - Display a single disk's info with formatting
 */
void print_disk_info(const DiskInfo *info);

/**
 * print_disk_summary - Display all disks in a fancy table
 */
void print_disk_summary(const DiskInfo *disks, int count);

/**
 * scan_and_display_disks - Scan all drives and display fancy table
 * 
 * Call this from kernel_main() BEFORE enabling paging/protected mode
 * (BIOS INT 13h only works in real mode or from real-mode shim)
 */
void scan_and_display_disks(void);

/**
 * scan_and_display_disks_simple - Scan and display simple list format
 */
void scan_and_display_disks_simple(void);

#endif /* DISK_SCANNER_H */