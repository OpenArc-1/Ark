/**
 * disk_scanner.c
 * Disk size scanner for ARK kernel with fancy output
 *
 * Scans all BIOS drives (0x80-0x8F) and displays sizes in a nice table
 */

#include <stdint.h>
#include "ark/types.h"
#include "ark/printk.h"

/* ─── Public result type ─────────────────────────────────────────────────── */

typedef struct {
    uint8_t  drive;           /* 0x80 = first HDD, 0x81 = second, …        */
    uint32_t sectors_lo;      /* low  32 bits of total LBA sector count     */
    uint32_t sectors_hi;      /* high 32 bits of total LBA sector count     */
    uint32_t total_mb;
    uint32_t total_gb_int;
    uint32_t total_gb_frac;   /* tenths: 0–9                                */
    uint8_t  method;          /* 1 = INT 13h extended, 2 = CHS fallback    */
    uint8_t  valid;
} DiskInfo;

/* ─── INT 13h AH=41h — check extension support ──────────────────────────── */

static int int13_check_ext(uint8_t drive)
{
    uint32_t eax   = 0x00004100;
    uint32_t ebx   = 0x000055AA;
    uint32_t edx   = (uint32_t)drive;
    uint32_t carry = 0;

    __asm__ volatile (
        "push %%es          \n"
        "int  $0x13         \n"
        "jc   1f            \n"
        "movl $0, %[cf]     \n"
        "jmp  2f            \n"
        "1: movl $1, %[cf]  \n"
        "2:                 \n"
        "pop  %%es          \n"
        : [cf] "+r" (carry),
          "+a"  (eax),
          "+b"  (ebx),
          "+d"  (edx)
        : /* no inputs */
        : "cc", "ecx", "esi", "edi", "memory"
    );

    return (!carry && (ebx & 0xFFFFu) == 0xAA55u) ? 1 : 0;
}

/* ─── INT 13h AH=48h — extended drive params (>8 GB) ───────────────────── */

static int int13_ext_params(uint8_t drive,
                             uint32_t *sectors_lo, uint32_t *sectors_hi)
{
    uint8_t buf[26];
    for (int i = 0; i < 26; i++) buf[i] = 0;
    buf[0] = 26;
    buf[1] = 0;

    uint32_t eax   = 0x00004800;
    uint32_t edx   = (uint32_t)drive;
    uint32_t esi   = (uint32_t)buf;
    uint32_t carry = 0;

    __asm__ volatile (
        "push %%es          \n"
        "int  $0x13         \n"
        "jc   1f            \n"
        "movl $0, %[cf]     \n"
        "jmp  2f            \n"
        "1: movl $1, %[cf]  \n"
        "2:                 \n"
        "pop  %%es          \n"
        : [cf] "+r" (carry),
          "+a"  (eax),
          "+d"  (edx),
          "+S"  (esi)
        : /* no inputs */
        : "cc", "ebx", "ecx", "edi", "memory"
    );

    if (carry) return 0;

    uint32_t lo, hi;
    __builtin_memcpy(&lo, buf + 16, 4);
    __builtin_memcpy(&hi, buf + 20, 4);
    *sectors_lo = lo;
    *sectors_hi = hi;
    return 1;
}

/* ─── INT 13h AH=08h — CHS geometry (fallback, ≤8 GB) ──────────────────── */

static int int13_chs_params(uint8_t drive, uint32_t *total_sectors)
{
    uint32_t eax   = 0x00000800;
    uint32_t ecx   = 0;
    uint32_t edx   = (uint32_t)drive;
    uint32_t carry = 0;

    __asm__ volatile (
        "push %%es          \n"
        "int  $0x13         \n"
        "jc   1f            \n"
        "movl $0, %[cf]     \n"
        "jmp  2f            \n"
        "1: movl $1, %[cf]  \n"
        "2:                 \n"
        "pop  %%es          \n"
        : [cf] "+r" (carry),
          "+a"  (eax),
          "+c"  (ecx),
          "+d"  (edx)
        : /* no inputs */
        : "cc", "ebx", "esi", "edi", "memory"
    );

    if (carry) return 0;

    uint32_t C = (((ecx & 0x00C0u) << 2) | ((ecx >> 8) & 0xFFu)) + 1u;
    uint32_t H = ((edx >> 8) & 0xFFu) + 1u;
    uint32_t S = ecx & 0x3Fu;
    *total_sectors = C * H * S;
    return 1;
}

/* ─── Convert sector count (hi:lo) to megabytes ─────────────────────────── */

static uint32_t sectors_to_mb(uint32_t lo, uint32_t hi)
{
    if (hi >= 2048u) return 0xFFFFFFFFu;
    return (hi * 2097152u) + (lo / 2048u);
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

int scan_disk(uint8_t drive, DiskInfo *info)
{
    uint32_t slo = 0, shi = 0;

    info->drive = drive;
    info->valid = 0;

    if (int13_check_ext(drive) && int13_ext_params(drive, &slo, &shi)) {
        info->method = 1;
    } else {
        uint32_t chs = 0;
        if (!int13_chs_params(drive, &chs)) return 0;
        slo = chs;
        shi = 0;
        info->method = 2;
    }

    info->sectors_lo    = slo;
    info->sectors_hi    = shi;
    info->total_mb      = sectors_to_mb(slo, shi);
    info->total_gb_int  = info->total_mb / 1024u;
    info->total_gb_frac = (info->total_mb % 1024u) * 10u / 1024u;
    info->valid         = 1;
    return 1;
}

int scan_all_disk(DiskInfo *results)
{
    int count = 0;
    for (int i = 0; i < 16; i++) {
        if (scan_disk((uint8_t)(0x80u + (uint8_t)i), &results[i])) {
            count++;
        } else {
            results[i].valid = 0;
            break;
        }
    }
    return count;
}

/* ─── Pretty Display Functions ───────────────────────────────────────────── */

/**
 * print_disk_info - Display a single disk's info with nice formatting
 */
void print_disk_info(const DiskInfo *info)
{
    if (!info->valid) return;

    // Drive name
    printk("  [disk] 0x%x: ", (u32)info->drive);
    
    // Size in GB
    if (info->total_gb_int > 0) {
        printk("%u.%u GB ", info->total_gb_int, info->total_gb_frac);
    } else {
        printk("%u MB ", info->total_mb);
    }
    
    // Size in MB (always show for precision)
    printk("(%u MB) ", info->total_mb);
    
    // Detection method
    if (info->method == 1) {
        printk("[LBA48]");
    } else {
        printk("[CHS]");
    }
    
    // Sector count (if available)
    if (info->sectors_hi > 0) {
        printk(" sectors: 0x%x%x", info->sectors_hi, info->sectors_lo);
    } else {
        printk(" sectors: %u", info->sectors_lo);
    }
    
    printk("\n");
}

/**
 * print_disk_summary - Display all disks in a nice table format
 */
void print_disk_summary(const DiskInfo *disks, int count)
{
    if (count == 0) {
        printk("[disk] No storage devices detected\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        if (disks[i].valid) {
            printk(" Drive 0x%x: ", (u32)disks[i].drive);
            
            // Right-align the size
            if (disks[i].total_gb_int >= 100) {
                printk("%u.%u GB ", disks[i].total_gb_int, disks[i].total_gb_frac);
            } else if (disks[i].total_gb_int >= 10) {
                printk(" %u.%u GB ", disks[i].total_gb_int, disks[i].total_gb_frac);
            } else if (disks[i].total_gb_int > 0) {
                printk("  %u.%u GB ", disks[i].total_gb_int, disks[i].total_gb_frac);
            } else {
                printk("   %u MB ", disks[i].total_mb);
            }
            
            // Method
            if (disks[i].method == 1) {
                printk("(LBA48)");
            } else {
                printk("(CHS)  ");
            }
            
            // Pad to column 58
            printk("                    ║\n");
        }
    }
    
    printk("[disk] Total devices found: %d\n", count);
    printk("\n");
}

/**
 * scan_and_display_disks - Convenience function: scan all and display results
 * 
 * Call this from kernel_main() after BIOS is still accessible (before paging)
 */
void scan_and_display_disks(void)
{
    DiskInfo disks[16];
    
    printk("[disk] Scanning BIOS storage devices...\n");
    int count = scan_all_disk(disks);
    
    if (count > 0) {
        print_disk_summary(disks, count);
    } else {
        printk("[disk] No storage devices detected\n");
    }
}

/**
 * scan_and_display_disks_simple - Simpler output without fancy borders
 */
void scan_and_display_disks_simple(void)
{
    DiskInfo disks[16];
    
    printk("[disk] Scanning storage devices...\n");
    int count = scan_all_disk(disks);
    
    if (count == 0) {
        printk("[disk] No devices found\n");
        return;
    }
    
    printk("[disk] Found %d device(s):\n", count);
    for (int i = 0; i < count; i++) {
        if (disks[i].valid) {
            print_disk_info(&disks[i]);
        }
    }
    printk("\n");
}