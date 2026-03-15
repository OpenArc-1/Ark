/*
 * arch/x86_64/smbios64.c — SMBIOS 2.x/3.x parser for Ark x86_64
 *
 * Uses the existing smbios_eps_t / smbios_hdr_t / smbios_info_t types
 * from include/hw/smbios.h.  Only adds the SMBIOS 3.x 64-bit EPS struct
 * (not in the header) and re-implements the API using identity-mapped
 * physical addresses (correct for Ark's flat map, no +0xC0000000 bias).
 */

/* Override PHYS_TO_VIRT before the header uses it (identity map for x86_64) */
#undef  PHYS_TO_VIRT
#define PHYS_TO_VIRT(addr)  ((void *)(uintptr_t)(uint32_t)(addr))

#include "hw/smbios.h"
#include <ark/types.h>
#include <ark/printk.h>

/* ── SMBIOS 3.x 64-bit Entry Point (not in smbios.h) ─────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  anchor[5];        /* "_SM3_" */
    uint8_t  checksum;
    uint8_t  eps_len;
    uint8_t  major_ver;
    uint8_t  minor_ver;
    uint8_t  docrev;
    uint8_t  eps_rev;
    uint8_t  reserved;
    uint32_t max_struct_size;
    uint64_t table_addr;
} smbios3_eps_t;

/* ── Scan range (BIOS ROM shadow) ─────────────────────────────────────────── */
#define SCAN_START  0x000F0000u
#define SCAN_END    0x000FFFFFu

/* ── State ────────────────────────────────────────────────────────────────── */
static smbios_info_t g_smbios;
static bool          g_ready = false;

/* ── Helpers ──────────────────────────────────────────────────────────────── */
static uint8_t checksum8(const uint8_t *p, usize n) {
    uint8_t s = 0;
    while (n--) s += *p++;
    return s;
}

static void strncpy64(char *dst, const char *src, usize n) {
    usize i = 0;
    while (i + 1 < n && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Return Nth string from string pool after a structure */
static const char *get_str(const smbios_hdr_t *hdr, uint8_t idx) {
    if (!idx) return "";
    const char *p = (const char *)hdr + hdr->length;
    uint8_t n = 1;
    while (*p || *(p + 1)) {
        if (n == idx) return p;
        while (*p) p++;
        p++;
        n++;
    }
    return "";
}

/* ── Table walker ─────────────────────────────────────────────────────────── */
static void walk_table(const uint8_t *tbl, uint32_t len,
                       uint16_t max_structs) {
    const uint8_t *p   = tbl;
    const uint8_t *end = tbl + len;
    uint16_t       cnt = 0;

    while (p < end && cnt < max_structs) {
        if (p + 4 > end) break;
        const smbios_hdr_t *hdr = (const smbios_hdr_t *)p;
        if (hdr->type == 127) break;
        if (hdr->length < 4) break;

        switch (hdr->type) {
        case 0: /* BIOS Information */
            strncpy64(g_smbios.bios_vendor,  get_str(hdr,1), SMBIOS_STR_MAX);
            strncpy64(g_smbios.bios_version, get_str(hdr,2), SMBIOS_STR_MAX);
            strncpy64(g_smbios.bios_date,    get_str(hdr,3), SMBIOS_STR_MAX);
            if (hdr->length >= 0x16) {
                g_smbios.bios_major = ((const uint8_t *)hdr)[0x14];
                g_smbios.bios_minor = ((const uint8_t *)hdr)[0x15];
            }
            break;
        case 1: /* System Information */
            strncpy64(g_smbios.sys_vendor,  get_str(hdr,1), SMBIOS_STR_MAX);
            strncpy64(g_smbios.sys_product, get_str(hdr,2), SMBIOS_STR_MAX);
            strncpy64(g_smbios.sys_version, get_str(hdr,3), SMBIOS_STR_MAX);
            strncpy64(g_smbios.sys_serial,  get_str(hdr,4), SMBIOS_STR_MAX);
            if (hdr->length >= 0x19)
                for (int i = 0; i < 16; i++)
                    g_smbios.sys_uuid[i] = ((const uint8_t *)hdr)[0x08 + i];
            if (hdr->length >= 0x1B) {
                strncpy64(g_smbios.sys_sku,    get_str(hdr,6), SMBIOS_STR_MAX);
                strncpy64(g_smbios.sys_family, get_str(hdr,7), SMBIOS_STR_MAX);
            }
            break;
        case 2: /* Baseboard */
            strncpy64(g_smbios.board_vendor,  get_str(hdr,1), SMBIOS_STR_MAX);
            strncpy64(g_smbios.board_product, get_str(hdr,2), SMBIOS_STR_MAX);
            strncpy64(g_smbios.board_version, get_str(hdr,3), SMBIOS_STR_MAX);
            strncpy64(g_smbios.board_serial,  get_str(hdr,4), SMBIOS_STR_MAX);
            break;
        case 3: /* Chassis */
            strncpy64(g_smbios.chassis_vendor,  get_str(hdr,1), SMBIOS_STR_MAX);
            strncpy64(g_smbios.chassis_version, get_str(hdr,3), SMBIOS_STR_MAX);
            strncpy64(g_smbios.chassis_serial,  get_str(hdr,4), SMBIOS_STR_MAX);
            if (hdr->length >= 0x06)
                g_smbios.chassis_type = ((const uint8_t *)hdr)[0x05] & 0x7F;
            break;
        case 4: /* Processor */
            if (g_smbios.cpu_count < SMBIOS_MAX_CPUS) {
                uint8_t ci = g_smbios.cpu_count++;
                const uint8_t *b = (const uint8_t *)hdr;
                strncpy64(g_smbios.cpus[ci].socket,
                           get_str(hdr,1), SMBIOS_STR_MAX);
                strncpy64(g_smbios.cpus[ci].manufacturer,
                           get_str(hdr,3), SMBIOS_STR_MAX);
                strncpy64(g_smbios.cpus[ci].version,
                           get_str(hdr,4), SMBIOS_STR_MAX);
                if (hdr->length >= 0x1A) {
                    g_smbios.cpus[ci].status        = b[0x18];
                    g_smbios.cpus[ci].max_speed_mhz =
                        (uint16_t)(b[0x14] | (b[0x15] << 8));
                    g_smbios.cpus[ci].cur_speed_mhz =
                        (uint16_t)(b[0x16] | (b[0x17] << 8));
                }
                if (hdr->length >= 0x28) {
                    g_smbios.cpus[ci].core_count   = b[0x23];
                    g_smbios.cpus[ci].thread_count = b[0x25];
                }
            }
            break;
        case 17: /* Memory Device */
            if (g_smbios.mem_count < SMBIOS_MAX_MEM_DEVICES) {
                uint8_t mi = g_smbios.mem_count++;
                const uint8_t *b = (const uint8_t *)hdr;
                if (hdr->length >= 0x0E) {
                    uint16_t sz = (uint16_t)(b[0x0C] | (b[0x0D] << 8));
                    if (sz && sz != 0xFFFF)
                        g_smbios.mem[mi].size_mb =
                            (sz & 0x8000) ? (sz & 0x7FFF) : (uint32_t)sz;
                }
                if (hdr->length >= 0x17)
                    g_smbios.mem[mi].speed_mhz =
                        (uint16_t)(b[0x15] | (b[0x16] << 8));
                strncpy64(g_smbios.mem[mi].locator,
                           get_str(hdr, b[0x10]), SMBIOS_STR_MAX);
                strncpy64(g_smbios.mem[mi].bank,
                           get_str(hdr, b[0x11]), SMBIOS_STR_MAX);
                strncpy64(g_smbios.mem[mi].manufacturer,
                           get_str(hdr, b[0x17]), SMBIOS_STR_MAX);
                strncpy64(g_smbios.mem[mi].part_number,
                           get_str(hdr, b[0x1A]), SMBIOS_STR_MAX);
            }
            break;
        default:
            break;
        }

        /* Skip past string pool (ends at double NUL) */
        const char *q = (const char *)p + hdr->length;
        while ((const uint8_t *)q < end) {
            if (q[0] == '\0' && q[1] == '\0') { q += 2; break; }
            q++;
        }
        p = (const uint8_t *)q;
        cnt++;
    }
}

/* ── EPS finders ──────────────────────────────────────────────────────────── */
static bool find_eps2(smbios_eps_t **out) {
    const uint8_t *p = (const uint8_t *)PHYS_TO_VIRT(SCAN_START);
    const uint8_t *e = (const uint8_t *)PHYS_TO_VIRT(SCAN_END);
    for (; p <= e - 16; p += 16) {
        if (p[0]!='_'||p[1]!='S'||p[2]!='M'||p[3]!='_') continue;
        smbios_eps_t *eps = (smbios_eps_t *)p;
        if (eps->length < sizeof(smbios_eps_t)) continue;
        if (checksum8(p, eps->length) != 0) continue;
        *out = eps;
        return true;
    }
    return false;
}

static bool find_eps3(smbios3_eps_t **out) {
    const uint8_t *p = (const uint8_t *)PHYS_TO_VIRT(SCAN_START);
    const uint8_t *e = (const uint8_t *)PHYS_TO_VIRT(SCAN_END);
    for (; p <= e - 24; p += 16) {
        if (p[0]!='_'||p[1]!='S'||p[2]!='M'||p[3]!='3'||p[4]!='_') continue;
        smbios3_eps_t *eps = (smbios3_eps_t *)p;
        if (checksum8(p, eps->eps_len) != 0) continue;
        *out = eps;
        return true;
    }
    return false;
}

/* ── Public API ───────────────────────────────────────────────────────────── */
bool smbios_init(void) {
    smbios_memset(&g_smbios, 0, sizeof(g_smbios));
    g_ready = false;

    /* Try SMBIOS 3.x first (64-bit table address) */
    smbios3_eps_t *eps3 = (smbios3_eps_t *)0;
    if (find_eps3(&eps3)) {
        g_smbios.major = eps3->major_ver;
        g_smbios.minor = eps3->minor_ver;
        printk("[SMBIOS] SMBIOS %u.%u (3.x) table=0x%llx\n",
               (unsigned)eps3->major_ver, (unsigned)eps3->minor_ver,
               (unsigned long long)eps3->table_addr);
        walk_table((const uint8_t *)(uintptr_t)eps3->table_addr,
                   eps3->max_struct_size, 0xFFFF);
        g_ready = true;
        return true;
    }

    /* Fall back to SMBIOS 2.x */
    smbios_eps_t *eps2 = (smbios_eps_t *)0;
    if (!find_eps2(&eps2)) {
        printk("[SMBIOS] EPS not found (0x%05X-0x%05X)\n",
               SCAN_START, SCAN_END);
        return false;
    }

    g_smbios.major = eps2->major_ver;
    g_smbios.minor = eps2->minor_ver;
    printk("[SMBIOS] SMBIOS %u.%u table=0x%08X len=%u n=%u\n",
           (unsigned)eps2->major_ver, (unsigned)eps2->minor_ver,
           (unsigned)eps2->table_addr,
           (unsigned)eps2->table_len,
           (unsigned)eps2->num_structs);

    walk_table((const uint8_t *)PHYS_TO_VIRT(eps2->table_addr),
               eps2->table_len, eps2->num_structs);
    g_ready = true;
    return true;
}

const smbios_info_t *smbios_get_info(void) {
    return g_ready ? &g_smbios : (smbios_info_t *)0;
}

void smbios_dump(void) {
    const smbios_info_t *i = &g_smbios;
    if (!g_ready) { printk("[SMBIOS] Not available\n"); return; }

    printk("SMBIOS %u.%u\n", i->major, i->minor);
    printk("[BIOS]    vendor  : %s\n", i->bios_vendor);
    printk("[BIOS]    version : %s\n", i->bios_version);
    printk("[BIOS]    date    : %s\n", i->bios_date);
    if (i->bios_major || i->bios_minor)
        printk("[BIOS]    release : %u.%u\n",
               (unsigned)i->bios_major, (unsigned)i->bios_minor);
    printk("[SYS]     vendor  : %s\n", i->sys_vendor);
    printk("[SYS]     product : %s\n", i->sys_product);
    printk("[SYS]     version : %s\n", i->sys_version);
    printk("[SYS]     serial  : %s\n", i->sys_serial);
    printk("[SYS]     sku     : %s\n", i->sys_sku);
    printk("[SYS]     family  : %s\n", i->sys_family);
    printk("[SYS]     uuid    : "
           "%02X%02X%02X%02X-%02X%02X-%02X%02X-"
           "%02X%02X-%02X%02X%02X%02X%02X%02X\n",
           i->sys_uuid[0],  i->sys_uuid[1],
           i->sys_uuid[2],  i->sys_uuid[3],
           i->sys_uuid[4],  i->sys_uuid[5],
           i->sys_uuid[6],  i->sys_uuid[7],
           i->sys_uuid[8],  i->sys_uuid[9],
           i->sys_uuid[10], i->sys_uuid[11],
           i->sys_uuid[12], i->sys_uuid[13],
           i->sys_uuid[14], i->sys_uuid[15]);
    printk("[BOARD]   vendor  : %s\n", i->board_vendor);
    printk("[BOARD]   product : %s\n", i->board_product);
    printk("[BOARD]   version : %s\n", i->board_version);
    printk("[BOARD]   serial  : %s\n", i->board_serial);
    printk("[CHASSIS] vendor  : %s\n", i->chassis_vendor);
    printk("[CHASSIS] version : %s\n", i->chassis_version);
    printk("[CHASSIS] serial  : %s\n", i->chassis_serial);
    printk("[CHASSIS] type    : 0x%02X\n", (unsigned)i->chassis_type);
    printk("[CPU]     count   : %u\n", (unsigned)i->cpu_count);
    for (uint8_t c = 0; c < i->cpu_count; c++) {
        printk("[CPU %u]   socket  : %s\n", c, i->cpus[c].socket);
        printk("[CPU %u]   vendor  : %s\n", c, i->cpus[c].manufacturer);
        printk("[CPU %u]   version : %s\n", c, i->cpus[c].version);
        printk("[CPU %u]   speed   : %u/%u MHz\n", c,
               (unsigned)i->cpus[c].cur_speed_mhz,
               (unsigned)i->cpus[c].max_speed_mhz);
        printk("[CPU %u]   cores   : %u  threads: %u\n", c,
               (unsigned)i->cpus[c].core_count,
               (unsigned)i->cpus[c].thread_count);
    }
    printk("[MEM]     slots   : %u\n", (unsigned)i->mem_count);
    for (uint8_t m = 0; m < i->mem_count; m++) {
        if (!i->mem[m].size_mb) {
            printk("[MEM %2u]  [empty]  %s / %s\n",
                   (unsigned)m, i->mem[m].locator, i->mem[m].bank);
            continue;
        }
        printk("[MEM %2u]  %s / %s  %lu MB  %u MHz  %s  p/n=%s\n",
               (unsigned)m,
               i->mem[m].locator, i->mem[m].bank,
               (unsigned long)i->mem[m].size_mb,
               (unsigned)i->mem[m].speed_mhz,
               i->mem[m].manufacturer,
               i->mem[m].part_number);
    }
}
