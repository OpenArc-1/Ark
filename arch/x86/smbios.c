/*
 * smbios.c - SMBIOS 2.x table parser (32-bit freestanding kernel)
 *
 * NO libc. NO <string.h>. Works with -ffreestanding -nostdlib.
 *
 * ── Porting: two macros to set ──────────────────────────────────────────
 *
 *   PHYS_TO_VIRT(addr)  physical uint32_t → kernel virtual pointer
 *     identity map  : (void *)(uint32_t)(addr)          ← default
 *     higher-half   : (void *)((uint32_t)(addr) + 0xC0000000u)
 *
 *   printk(fmt, ...)   your serial/VGA log function
 *
 * ────────────────────────────────────────────────────────────────────────
 */

#include "hw/smbios.h"
#include <ark/printk.h>

/* =========================================================================
 * Platform glue
 * ====================================================================== */

#ifndef PHYS_TO_VIRT
#define PHYS_TO_VIRT(addr)  ((void *)((uint32_t)(addr) + 0xC0000000u))
#endif

/* =========================================================================
 * Private helpers (no libc)
 * ====================================================================== */

/* Bounded string copy – always NUL-terminates */
static void scopy(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (i < max - 1u && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Byte-sum checksum – valid region sums to 0 mod 256 */
static bool checksum_ok(const uint8_t *buf, uint8_t len)
{
    uint8_t sum = 0, i;
    for (i = 0; i < len; i++)
        sum += buf[i];
    return (sum == 0);
}

/*
 * get_string() – fetch a NUL-terminated string from the SMBIOS string area.
 * Strings are 1-indexed; 0 → "".
 * The string block ends with a double-NUL terminator.
 */
static const char *get_string(const smbios_hdr_t *hdr, uint8_t idx)
{
    const char *p;
    uint8_t n;

    if (idx == 0)
        return "";

    p = (const char *)hdr + hdr->length;
    n = 1;

    while (!(*p == '\0' && *(p + 1) == '\0')) {
        if (n == idx)
            return p;
        while (*p != '\0')
            p++;
        p++;
        n++;
    }
    return "";
}

/* =========================================================================
 * Per-type parsers
 * ====================================================================== */

static void parse_type0(const smbios_hdr_t *hdr, smbios_info_t *out)
{
    const smbios_type0_t *t;
    if (hdr->length < (uint8_t)sizeof(smbios_type0_t))
        return;
    t = (const smbios_type0_t *)hdr;
    scopy(out->bios_vendor,  get_string(hdr, t->vendor),      SMBIOS_STR_MAX);
    scopy(out->bios_version, get_string(hdr, t->version),     SMBIOS_STR_MAX);
    scopy(out->bios_date,    get_string(hdr, t->release_date), SMBIOS_STR_MAX);
    if (hdr->length >= 0x14u) {
        out->bios_major = t->bios_major;
        out->bios_minor = t->bios_minor;
    }
}

static void parse_type1(const smbios_hdr_t *hdr, smbios_info_t *out)
{
    const smbios_type1_t *t;
    if (hdr->length < 0x08u)
        return;
    t = (const smbios_type1_t *)hdr;
    scopy(out->sys_vendor,  get_string(hdr, t->manufacturer),  SMBIOS_STR_MAX);
    scopy(out->sys_product, get_string(hdr, t->product_name),  SMBIOS_STR_MAX);
    scopy(out->sys_version, get_string(hdr, t->version),       SMBIOS_STR_MAX);
    scopy(out->sys_serial,  get_string(hdr, t->serial_number), SMBIOS_STR_MAX);
    if (hdr->length >= (uint8_t)sizeof(smbios_type1_t)) {
        smbios_memcpy(out->sys_uuid, t->uuid, 16);
        scopy(out->sys_sku,    get_string(hdr, t->sku),    SMBIOS_STR_MAX);
        scopy(out->sys_family, get_string(hdr, t->family), SMBIOS_STR_MAX);
    }
}

static void parse_type2(const smbios_hdr_t *hdr, smbios_info_t *out)
{
    const smbios_type2_t *t;
    if (hdr->length < 0x08u)
        return;
    t = (const smbios_type2_t *)hdr;
    scopy(out->board_vendor,  get_string(hdr, t->manufacturer),  SMBIOS_STR_MAX);
    scopy(out->board_product, get_string(hdr, t->product),       SMBIOS_STR_MAX);
    scopy(out->board_version, get_string(hdr, t->version),       SMBIOS_STR_MAX);
    scopy(out->board_serial,  get_string(hdr, t->serial_number), SMBIOS_STR_MAX);
}

static void parse_type3(const smbios_hdr_t *hdr, smbios_info_t *out)
{
    const smbios_type3_t *t;
    if (hdr->length < 0x09u)
        return;
    t = (const smbios_type3_t *)hdr;
    scopy(out->chassis_vendor,  get_string(hdr, t->manufacturer),  SMBIOS_STR_MAX);
    scopy(out->chassis_version, get_string(hdr, t->version),       SMBIOS_STR_MAX);
    scopy(out->chassis_serial,  get_string(hdr, t->serial_number), SMBIOS_STR_MAX);
    out->chassis_type = t->type & 0x7Fu;
}

static void parse_type4(const smbios_hdr_t *hdr, smbios_info_t *out)
{
    const smbios_type4_t *t;
    smbios_cpu_t *cpu;
    if (out->cpu_count >= SMBIOS_MAX_CPUS || hdr->length < 0x1Au)
        return;
    t   = (const smbios_type4_t *)hdr;
    cpu = &out->cpus[out->cpu_count++];
    scopy(cpu->socket,       get_string(hdr, t->socket),       SMBIOS_STR_MAX);
    scopy(cpu->manufacturer, get_string(hdr, t->manufacturer), SMBIOS_STR_MAX);
    scopy(cpu->version,      get_string(hdr, t->version),      SMBIOS_STR_MAX);
    scopy(cpu->serial,       get_string(hdr, t->serial),       SMBIOS_STR_MAX);
    cpu->type          = t->type;
    cpu->family        = t->family;
    cpu->max_speed_mhz = t->max_speed;
    cpu->cur_speed_mhz = t->cur_speed;
    cpu->status        = t->status;
    if (hdr->length >= 0x24u) {
        cpu->core_count   = t->core_count;
        cpu->thread_count = t->thread_count;
    }
}

static void parse_type17(const smbios_hdr_t *hdr, smbios_info_t *out)
{
    const smbios_type17_t *t;
    smbios_mem_t *m;
    if (out->mem_count >= SMBIOS_MAX_MEM_DEVICES || hdr->length < 0x15u)
        return;
    t = (const smbios_type17_t *)hdr;
    m = &out->mem[out->mem_count++];
    scopy(m->locator,      get_string(hdr, t->device_locator), SMBIOS_STR_MAX);
    scopy(m->bank,         get_string(hdr, t->bank_locator),   SMBIOS_STR_MAX);
    scopy(m->manufacturer, get_string(hdr, t->manufacturer),   SMBIOS_STR_MAX);
    scopy(m->serial,       get_string(hdr, t->serial),         SMBIOS_STR_MAX);
    scopy(m->part_number,  get_string(hdr, t->part_number),    SMBIOS_STR_MAX);
    m->form_factor = t->form_factor;
    m->memory_type = t->memory_type;
    m->speed_mhz   = t->speed;
    m->total_width = t->total_width;
    m->data_width  = t->data_width;
    if (t->size == 0x7FFFu && hdr->length >= 0x1Eu)
        m->size_mb = t->ext_size & 0x7FFFFFFFu;
    else if (t->size & 0x8000u)
        m->size_mb = (uint32_t)(t->size & 0x7FFFu) / 1024u;
    else
        m->size_mb = (uint32_t)t->size;
}

/* =========================================================================
 * Table walker
 * ====================================================================== */

static void walk_table(const uint8_t *base, uint16_t tlen,
                        uint16_t nstructs, smbios_info_t *out)
{
    const uint8_t *ptr   = base;
    const uint8_t *limit = base + tlen;
    uint16_t       count = 0;

    while (ptr + sizeof(smbios_hdr_t) <= limit) {
        const smbios_hdr_t *hdr = (const smbios_hdr_t *)ptr;

        if (hdr->type == SMBIOS_TYPE_END)
            break;

        switch (hdr->type) {
        case SMBIOS_TYPE_BIOS:      parse_type0(hdr, out);  break;
        case SMBIOS_TYPE_SYSTEM:    parse_type1(hdr, out);  break;
        case SMBIOS_TYPE_BASEBOARD: parse_type2(hdr, out);  break;
        case SMBIOS_TYPE_CHASSIS:   parse_type3(hdr, out);  break;
        case SMBIOS_TYPE_PROCESSOR: parse_type4(hdr, out);  break;
        case SMBIOS_TYPE_MEMDEVICE: parse_type17(hdr, out); break;
        default:                                             break;
        }

        ptr += hdr->length;                             /* skip fixed part */
        while ((ptr + 1) < limit &&                     /* skip string area */
               !(ptr[0] == 0 && ptr[1] == 0))
            ptr++;
        ptr += 2;                                       /* skip double-NUL */

        if (++count == nstructs)
            break;
    }
}

/* =========================================================================
 * Entry-point search  (0x000F0000 – 0x000FFFFF, 16-byte aligned)
 * ====================================================================== */

#define SCAN_START  0x000F0000u
#define SCAN_END    0x000FFFFFu

static bool find_eps(smbios_eps_t **out)
{
    const uint8_t *p   = (const uint8_t *)PHYS_TO_VIRT(SCAN_START);
    const uint8_t *end = (const uint8_t *)PHYS_TO_VIRT(SCAN_END);

    for (; p + sizeof(smbios_eps_t) <= end; p += 16) {
        const smbios_eps_t *eps;

        if (p[0] != '_' || p[1] != 'S' || p[2] != 'M' || p[3] != '_')
            continue;

        eps = (const smbios_eps_t *)p;

        if (eps->length < 0x1Fu)
            continue;
        if (!checksum_ok(p, eps->length))
            continue;

        /* Validate intermediate "_DMI_" anchor */
        if (eps->dmi_anchor[0] != '_' || eps->dmi_anchor[1] != 'D' ||
            eps->dmi_anchor[2] != 'M' || eps->dmi_anchor[3] != 'I' ||
            eps->dmi_anchor[4] != '_')
            continue;
        if (!checksum_ok((const uint8_t *)&eps->dmi_anchor, 15))
            continue;

        *out = (smbios_eps_t *)eps;
        return true;
    }
    return false;
}

/* =========================================================================
 * Module state
 * ====================================================================== */

static smbios_info_t g_smbios;
static bool          g_ready = false;

/* =========================================================================
 * Public API
 * ====================================================================== */

/**
 * smbios_init() – find and parse the SMBIOS 2.x tables.
 * Call once at kernel boot after your physical memory map is live.
 */
bool smbios_init(void)
{
    smbios_eps_t *eps = (smbios_eps_t *)0;

    smbios_memset(&g_smbios, 0, sizeof(g_smbios));

    if (!find_eps(&eps)) {
        printk("[SMBIOS] EPS not found (0x%05X-0x%05X)\n",
                SCAN_START, SCAN_END);
        return false;
    }

    g_smbios.major = eps->major_ver;
    g_smbios.minor = eps->minor_ver;

    printk("[SMBIOS] SMBIOS %u.%u at 0x%08X  table=0x%08X  len=%u  n=%u\n",
            (unsigned)eps->major_ver, (unsigned)eps->minor_ver,
            (uint32_t)(uintptr_t)eps,
            (uint32_t)eps->table_addr,
            (unsigned)eps->table_len,
            (unsigned)eps->num_structs);

    walk_table((const uint8_t *)PHYS_TO_VIRT(eps->table_addr),
               eps->table_len, eps->num_structs, &g_smbios);

    g_ready = true;
    return true;
}

/**
 * smbios_get_info() – pointer to the parsed info block, or NULL on failure.
 */
const smbios_info_t *smbios_get_info(void)
{
    return g_ready ? &g_smbios : (smbios_info_t *)0;
}

/**
 * smbios_dump() – print a human-readable hardware summary via printk.
 */
void smbios_dump(void)
{
    const smbios_info_t *i = &g_smbios;
    uint8_t c, m;

    if (!g_ready) {
        printk("[SMBIOS] Not available\n");
        return;
    }

    printk("SMBIOS %u.%u \n", i->major, i->minor);

    printk("[BIOS]    vendor  : %s\n",    i->bios_vendor);
    printk("[BIOS]    version : %s\n",    i->bios_version);
    printk("[BIOS]    date    : %s\n",    i->bios_date);
    if (i->bios_major || i->bios_minor) {
        printk("[BIOS]    release : %u.%u\n",
                (unsigned)i->bios_major, (unsigned)i->bios_minor);
    }

    printk("[SYS]     vendor  : %s\n",    i->sys_vendor);
    printk("[SYS]     product : %s\n",    i->sys_product);
    printk("[SYS]     version : %s\n",    i->sys_version);
    printk("[SYS]     serial  : %s\n",    i->sys_serial);
    printk("[SYS]     sku     : %s\n",    i->sys_sku);
    printk("[SYS]     family  : %s\n",    i->sys_family);
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

    printk("[BOARD]   vendor  : %s\n",    i->board_vendor);
    printk("[BOARD]   product : %s\n",    i->board_product);
    printk("[BOARD]   version : %s\n",    i->board_version);
    printk("[BOARD]   serial  : %s\n",    i->board_serial);

    printk("[CHASSIS] vendor  : %s\n",    i->chassis_vendor);
    printk("[CHASSIS] version : %s\n",    i->chassis_version);
    printk("[CHASSIS] serial  : %s\n",    i->chassis_serial);
    printk("[CHASSIS] type    : 0x%02X\n", (unsigned)i->chassis_type);

    printk("[CPU]     count   : %u\n",    (unsigned)i->cpu_count);
    for (c = 0; c < i->cpu_count; c++) {
        printk("[CPU %u]   socket  : %s\n",         c, i->cpus[c].socket);
        printk("[CPU %u]   vendor  : %s\n",         c, i->cpus[c].manufacturer);
        printk("[CPU %u]   version : %s\n",         c, i->cpus[c].version);
        printk("[CPU %u]   speed   : %u/%u MHz\n",  c,
                (unsigned)i->cpus[c].cur_speed_mhz,
                (unsigned)i->cpus[c].max_speed_mhz);
        printk("[CPU %u]   cores   : %u  threads: %u\n", c,
                (unsigned)i->cpus[c].core_count,
                (unsigned)i->cpus[c].thread_count);
        printk("[CPU %u]   status  : 0x%02X (%s)\n", c,
                (unsigned)i->cpus[c].status,
                (i->cpus[c].status & 0x01u) ? "populated" : "empty");
    }

    printk("[MEM]     slots   : %u\n", (unsigned)i->mem_count);
    for (m = 0; m < i->mem_count; m++) {
        if (i->mem[m].size_mb == 0) {
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