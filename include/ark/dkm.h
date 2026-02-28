/*
 * Vendor metadata macros for DKM modules.
 *
 * Place these in the top-level scope of your module source file:
 *
 *   __vendor_mod("mydriver");
 *   __vendor_ver("1.0");
 *
 * ark-gcc places the strings in dedicated ELF sections (.vendor_mod /
 * .vendor_ver) so the kernel loader can read them before executing the
 * module.  When loaded, the kernel will print:
 *
 *   => mydriver 1.0
 *
 * or, if module_init() returns non-zero:
 *
 *   => ...failed to init the dkm
 */
#define __vendor_mod(name) \
    static const char __dkm_vendor_mod[] \
        __attribute__((section(".vendor_mod"), used)) = (name)

#define __vendor_ver(ver) \
    static const char __dkm_vendor_ver[] \
        __attribute__((section(".vendor_ver"), used)) = (ver)

/*
 * Dynamic Kernel Module (DKM) loader API
 *
 * Modules can be loaded at runtime via direct kernel API or from userspace.
 * A module is expected to be a 32-bit ELF binary whose entry point has one of:
 *
 *   - int module_init(const ark_kernel_api_t *api);  [full kernel access]
 *   - int module_init(void);                         [minimal/standalone]
 *
 * The loader simply maps the module image into a static pool and invokes the
 * entrypoint. Modules should be position-independent and self-contained.
 *
 * Building a standalone module (no kernel API needed):
 *
 *   ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init \
 *       my_module.c -o my_module.elf
 *
 * Building a module with full kernel API:
 *
 *   ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init \
 *       -I include my_module.c -o my_module.elf
 *
 * Then load via strapper or direct kernel call to dkm_load().
 */

#pragma once

#include "ark/types.h"
#include "ark/init_api.h"   /* defines ark_kernel_api_t (struct ark_kernel_api) */

#define DKM_MAX_NAME   64
#define DKM_MAX_MODULES 16
#define DKM_POOL_SIZE  (1024 * 1024)   /* 1MB for module code/data */

/* record for each loaded module */
typedef struct dkm_module {
    char name[DKM_MAX_NAME];    /* basename of path, nul-terminated */
    u8   *mem;                  /* pointer into loader pool */
    u32   size;                /* number of bytes occupied in pool */
} dkm_module_t;

/* initialize the subsystem (called from kernel boot) */
void dkm_init(void);

/* userspace/library-visible helper; return 0 on success or negative error */
int dkm_load(const char *path);
int dkm_unload(const char *name);
void dkm_list(void);

/* kernel-only helper used by syscalls */
int dkm_sys_load(const char *path);
int dkm_sys_unload(const char *name);
void dkm_sys_list(void);

/* When a module is executed it receives this pointer just like init.bin. */
const ark_kernel_api_t *dkm_kernel_api(void);

/*
 * Read vendor metadata from a raw ELF image before executing it.
 * Fills mod_name (up to mod_len bytes) and mod_ver (up to ver_len bytes).
 * Returns 1 if at least one tag was found, 0 otherwise.
 */
int dkm_read_vendor_info(const u8 *elf_data, u32 elf_size,
                         char *mod_name, u32 mod_len,
                         char *mod_ver,  u32 ver_len);