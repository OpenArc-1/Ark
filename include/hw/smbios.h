/*
 * smbios.h - SMBIOS 2.x table parser (32-bit freestanding kernel)
 *
 * NO libc dependencies. All string/memory helpers are inlined here.
 * Targets SMBIOS 2.x "_SM_" 32-bit entry point.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 * Freestanding memory/string helpers
 * (Do NOT include <string.h> in a freestanding kernel build)
 * ====================================================================== */

static inline void *smbios_memset(void *dst, int c, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = (uint8_t)c;
    return dst;
}

static inline void *smbios_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static inline int smbios_memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

/* ── Tunables ─────────────────────────────────────────────────────────── */

#define SMBIOS_STR_MAX         64
#define SMBIOS_MAX_CPUS         4
#define SMBIOS_MAX_MEM_DEVICES 16

/* ── Structure type IDs ───────────────────────────────────────────────── */

#define SMBIOS_TYPE_BIOS        0
#define SMBIOS_TYPE_SYSTEM      1
#define SMBIOS_TYPE_BASEBOARD   2
#define SMBIOS_TYPE_CHASSIS     3
#define SMBIOS_TYPE_PROCESSOR   4
#define SMBIOS_TYPE_MEMARRAY   16
#define SMBIOS_TYPE_MEMDEVICE  17
#define SMBIOS_TYPE_END       127

/* ── 32-bit Entry Point ("_SM_") ─────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  anchor[4];               /* "_SM_"  */
    uint8_t  checksum;
    uint8_t  length;
    uint8_t  major_ver;
    uint8_t  minor_ver;
    uint16_t max_struct_size;
    uint8_t  revision;
    uint8_t  formatted[5];
    uint8_t  dmi_anchor[5];           /* "_DMI_" */
    uint8_t  dmi_checksum;
    uint16_t table_len;
    uint32_t table_addr;
    uint16_t num_structs;
    uint8_t  bcd_rev;
} smbios_eps_t;

/* ── Common structure header ─────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
} smbios_hdr_t;

/* ── Type 0 – BIOS Information ───────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    smbios_hdr_t hdr;
    uint8_t  vendor;
    uint8_t  version;
    uint16_t start_segment;
    uint8_t  release_date;
    uint8_t  rom_size;
    uint64_t characteristics;
    uint8_t  ext1;
    uint8_t  ext2;
    uint8_t  bios_major;
    uint8_t  bios_minor;
    uint8_t  ec_major;
    uint8_t  ec_minor;
} smbios_type0_t;

/* ── Type 1 – System Information ─────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    smbios_hdr_t hdr;
    uint8_t  manufacturer;
    uint8_t  product_name;
    uint8_t  version;
    uint8_t  serial_number;
    uint8_t  uuid[16];
    uint8_t  wakeup_type;
    uint8_t  sku;
    uint8_t  family;
} smbios_type1_t;

/* ── Type 2 – Baseboard Information ─────────────────────────────────── */

typedef struct __attribute__((packed)) {
    smbios_hdr_t hdr;
    uint8_t  manufacturer;
    uint8_t  product;
    uint8_t  version;
    uint8_t  serial_number;
    uint8_t  asset_tag;
    uint8_t  features;
    uint8_t  location;
    uint16_t chassis_handle;
    uint8_t  board_type;
    uint8_t  num_handles;
} smbios_type2_t;

/* ── Type 3 – Chassis Information ────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    smbios_hdr_t hdr;
    uint8_t  manufacturer;
    uint8_t  type;
    uint8_t  version;
    uint8_t  serial_number;
    uint8_t  asset_tag;
    uint8_t  boot_state;
    uint8_t  power_supply_state;
    uint8_t  thermal_state;
    uint8_t  security_status;
    uint32_t oem_defined;
    uint8_t  height;
    uint8_t  num_power_cords;
} smbios_type3_t;

/* ── Type 4 – Processor Information ─────────────────────────────────── */

typedef struct __attribute__((packed)) {
    smbios_hdr_t hdr;
    uint8_t  socket;
    uint8_t  type;
    uint8_t  family;
    uint8_t  manufacturer;
    uint64_t id;
    uint8_t  version;
    uint8_t  voltage;
    uint16_t ext_clock;
    uint16_t max_speed;
    uint16_t cur_speed;
    uint8_t  status;
    uint8_t  upgrade;
    uint16_t l1_handle;
    uint16_t l2_handle;
    uint16_t l3_handle;
    uint8_t  serial;
    uint8_t  asset_tag;
    uint8_t  part_number;
    uint8_t  core_count;
    uint8_t  cores_enabled;
    uint8_t  thread_count;
    uint16_t characteristics;
} smbios_type4_t;

/* ── Type 17 – Memory Device ─────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    smbios_hdr_t hdr;
    uint16_t array_handle;
    uint16_t error_handle;
    uint16_t total_width;
    uint16_t data_width;
    uint16_t size;
    uint8_t  form_factor;
    uint8_t  device_set;
    uint8_t  device_locator;
    uint8_t  bank_locator;
    uint8_t  memory_type;
    uint16_t type_detail;
    uint16_t speed;
    uint8_t  manufacturer;
    uint8_t  serial;
    uint8_t  asset_tag;
    uint8_t  part_number;
    uint8_t  attributes;
    uint32_t ext_size;
    uint16_t cfg_speed;
} smbios_type17_t;

/* ── Parsed result structures ────────────────────────────────────────── */

typedef struct {
    char     socket[SMBIOS_STR_MAX];
    char     manufacturer[SMBIOS_STR_MAX];
    char     version[SMBIOS_STR_MAX];
    char     serial[SMBIOS_STR_MAX];
    uint8_t  type;
    uint8_t  family;
    uint16_t max_speed_mhz;
    uint16_t cur_speed_mhz;
    uint8_t  core_count;
    uint8_t  thread_count;
    uint8_t  status;
} smbios_cpu_t;

typedef struct {
    char     locator[SMBIOS_STR_MAX];
    char     bank[SMBIOS_STR_MAX];
    char     manufacturer[SMBIOS_STR_MAX];
    char     serial[SMBIOS_STR_MAX];
    char     part_number[SMBIOS_STR_MAX];
    uint8_t  form_factor;
    uint8_t  memory_type;
    uint16_t speed_mhz;
    uint16_t total_width;
    uint16_t data_width;
    uint32_t size_mb;
} smbios_mem_t;

typedef struct {
    uint8_t  major;
    uint8_t  minor;

    char     bios_vendor[SMBIOS_STR_MAX];
    char     bios_version[SMBIOS_STR_MAX];
    char     bios_date[SMBIOS_STR_MAX];
    uint8_t  bios_major;
    uint8_t  bios_minor;

    char     sys_vendor[SMBIOS_STR_MAX];
    char     sys_product[SMBIOS_STR_MAX];
    char     sys_version[SMBIOS_STR_MAX];
    char     sys_serial[SMBIOS_STR_MAX];
    char     sys_sku[SMBIOS_STR_MAX];
    char     sys_family[SMBIOS_STR_MAX];
    uint8_t  sys_uuid[16];

    char     board_vendor[SMBIOS_STR_MAX];
    char     board_product[SMBIOS_STR_MAX];
    char     board_version[SMBIOS_STR_MAX];
    char     board_serial[SMBIOS_STR_MAX];

    char     chassis_vendor[SMBIOS_STR_MAX];
    char     chassis_version[SMBIOS_STR_MAX];
    char     chassis_serial[SMBIOS_STR_MAX];
    uint8_t  chassis_type;

    uint8_t      cpu_count;
    smbios_cpu_t cpus[SMBIOS_MAX_CPUS];

    uint8_t      mem_count;
    smbios_mem_t mem[SMBIOS_MAX_MEM_DEVICES];
} smbios_info_t;

/* ── Public API ──────────────────────────────────────────────────────── */

bool                  smbios_init(void);
const smbios_info_t  *smbios_get_info(void);
void                  smbios_dump(void);