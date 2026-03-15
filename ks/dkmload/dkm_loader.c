#include "ark/dkm.h"
#include "ark/printk.h"
#include "ark/vfs.h"
#include "ark/elf_loader.h"
#include "ark/init_api.h"

/*
 * Very simple dynamic module loader for Ark.
 *
 * The implementation uses a fixed-size memory pool and a fixed limit on the
 * number of modules.  When a module is requested the kernel copies the
 * corresponding file from the VFS into the pool and invokes its entrypoint.
 *
 * Modules must be linked as 32-bit ELF binaries that are position-independent
 * and whose entrypoint matches `int module_init(const ark_kernel_api_t *)`.
 * The loader does **not** perform relocations; any calls to kernel services
 * should go through the provided kernel API pointer.  Unloading only removes
 * the record from the list; the memory is not reclaimed.
 *
 * Vendor metadata:
 *   Modules may embed name/version using the macros from include/ark/dkm.h:
 *     __vendor_mod("mydriver");
 *     __vendor_ver("1.0");
 *   The loader reads these from ELF sections .vendor_mod / .vendor_ver and
 *   prints them as:
 *     => mydriver 1.0
 *   On init failure the loader prints:
 *     => ...failed to init the dkm
 */

/* ------------------------------------------------------------------ */
/* ELF structures for section-header scanning                         */
/* ------------------------------------------------------------------ */

#define ELF_MAGIC32 0x464c457fu

typedef struct {
    u32 magic; u8 class; u8 data; u8 version; u8 os_abi;
    u8 abi_version; u8 pad[7];
    u16 type; u16 machine; u32 version2; u32 entry;
    u32 phoff; u32 shoff; u32 flags;
    u16 ehsize; u16 phentsize; u16 phnum;
    u16 shentsize; u16 shnum; u16 shstrndx;
} dkm_elf32_hdr_t;

typedef struct {
    u32 sh_name; u32 sh_type; u32 sh_flags; u32 sh_addr;
    u32 sh_offset; u32 sh_size; u32 sh_link; u32 sh_info;
    u32 sh_addralign; u32 sh_entsize;
} dkm_elf32_shdr_t;

/* Copy at most (out_len-1) bytes from src (not necessarily NUL-terminated)
   into out, then NUL-terminate. */
static void safe_copy(char *out, u32 out_len, const char *src, u32 src_len) {
    u32 n = src_len < out_len - 1 ? src_len : out_len - 1;
    for (u32 i = 0; i < n; i++) out[i] = src[i];
    out[n] = '\0';
}

/* Simple strcmp without libc */
static int dkm_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Read .vendor_mod / .vendor_ver section contents from a raw ELF image. */
int dkm_read_vendor_info(const u8 *data, u32 size,
                         char *mod_name, u32 mod_len,
                         char *mod_ver,  u32 ver_len)
{
    if (mod_len) mod_name[0] = '\0';
    if (ver_len) mod_ver[0]  = '\0';

    if (!data || size < sizeof(dkm_elf32_hdr_t)) return 0;

    const dkm_elf32_hdr_t *eh = (const dkm_elf32_hdr_t *)data;
    if (eh->magic != ELF_MAGIC32) return 0;          /* not ELF */
    if (eh->shoff == 0 || eh->shnum == 0) return 0;  /* no section table */

    /* Bounds check section headers */
    if ((u32)eh->shoff + (u32)eh->shnum * sizeof(dkm_elf32_shdr_t) > size)
        return 0;

    const dkm_elf32_shdr_t *shdrs =
        (const dkm_elf32_shdr_t *)(data + eh->shoff);

    /* Section-name string table */
    if (eh->shstrndx >= eh->shnum) return 0;
    const dkm_elf32_shdr_t *shstr_hdr = &shdrs[eh->shstrndx];
    if (shstr_hdr->sh_offset + shstr_hdr->sh_size > size) return 0;
    const char *shstrtab = (const char *)(data + shstr_hdr->sh_offset);

    int found = 0;
    for (u16 i = 0; i < eh->shnum; i++) {
        const dkm_elf32_shdr_t *sh = &shdrs[i];
        if (sh->sh_name >= shstr_hdr->sh_size) continue;
        const char *name = shstrtab + sh->sh_name;

        /* Bounds-check section content */
        if (sh->sh_offset + sh->sh_size > size) continue;
        const char *content = (const char *)(data + sh->sh_offset);

        if (mod_len && dkm_strcmp(name, ".vendor_mod") == 0) {
            safe_copy(mod_name, mod_len, content, sh->sh_size);
            found = 1;
        } else if (ver_len && dkm_strcmp(name, ".vendor_ver") == 0) {
            safe_copy(mod_ver, ver_len, content, sh->sh_size);
            found = 1;
        }
    }
    return found;
}

static dkm_module_t g_modules[DKM_MAX_MODULES];
static u32 g_module_count = 0;
static u8  g_pool[DKM_POOL_SIZE];
static u32 g_pool_used = 0;

const ark_kernel_api_t *dkm_kernel_api(void) {
    return ark_kernel_api();
}

void dkm_init(void) {
    g_module_count = 0;
    g_pool_used = 0;
}

/* internal helper to get basename from a path */
static void get_basename(const char *path, char *out, u32 outlen) {
    const char *p = path;
    const char *last = p;
    while (*p) {
        if (*p == '/')
            last = p + 1;
        p++;
    }
    /* copy up to outlen-1 bytes */
    u32 i = 0;
    while (i + 1 < outlen && last[i]) {
        out[i] = last[i];
        i++;
    }
    out[i] = '\0';
}

int dkm_load(const char *path) {
    /* public wrapper used by kernel code */
    return dkm_sys_load(path);
}

int dkm_unload(const char *name) {
    return dkm_sys_unload(name);
}

void dkm_list(void) {
    dkm_sys_list();
}

/* syscall-visible implementations -------------------------------------------------- */

int dkm_sys_load(const char *path) {
    if (g_module_count >= DKM_MAX_MODULES) {
        printk(T, "dkm: module table full\n");
        return -1;
    }

    int fd = vfs_open(path);
    if (fd < 0) {
        printk(T, "dkm: cannot open %s\n", path);
        return -1;
    }

    u32 sz = vfs_file_size(fd);
    if (sz == 0) {
        printk(T, "dkm: %s is empty\n", path);
        vfs_close(fd);
        return -1;
    }

    if (sz > DKM_POOL_SIZE - g_pool_used) {
        printk(T, "dkm: module %s (%u bytes) too large for pool\n", path, sz);
        vfs_close(fd);
        return -1;
    }

    u8 *dest = g_pool + g_pool_used;
    int r = vfs_read(fd, dest, sz);
    vfs_close(fd);
    if (r < 0 || (u32)r != sz) {
        printk(T, "dkm: failed to read %s\n", path);
        return -1;
    }

    /* Read vendor metadata from ELF sections before executing */
    char vmod[64] = {0};
    char vver[32] = {0};
    dkm_read_vendor_info(dest, sz, vmod, sizeof(vmod), vver, sizeof(vver));

    /* run the module entrypoint.  elf_execute handles both ELF binaries and
       raw code; we simply pass the kernel API pointer so the module can call
       back into the kernel. */
    int rc = elf_execute(dest, sz, ark_kernel_api());
    if (rc != 0) {
        printk(T, "=> ...failed to init the dkm\n");
        return rc;
    }

    /* Print vendor banner: "=> <name> <version>" */
    if (vmod[0] && vver[0])
        printk(T, "=> %s %s\n", vmod, vver);
    else if (vmod[0])
        printk(T, "=> %s\n", vmod);
    else {
        /* fallback: use basename */
        char bname[DKM_MAX_NAME];
        get_basename(path, bname, sizeof(bname));
        printk(T, "=> %s\n", bname);
    }

    /* record the module in our table */
    get_basename(path, g_modules[g_module_count].name, DKM_MAX_NAME);
    g_modules[g_module_count].mem  = dest;
    g_modules[g_module_count].size = sz;
    g_module_count++;

    printk(T, "dkm: loaded %s (%u bytes)\n", path, sz);
    g_pool_used += sz;
    return 0;
}

int dkm_sys_unload(const char *name) {
    for (u32 i = 0; i < g_module_count; i++) {
        /* simple string compare */
        const char *a = g_modules[i].name;
        const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') {
            printk(T, "dkm: unloading %s\n", name);
            /* shift remaining entries down */
            for (u32 j = i; j + 1 < g_module_count; j++)
                g_modules[j] = g_modules[j + 1];
            g_module_count--;
            return 0;
        }
    }
    printk(T, "dkm: module %s not found\n", name);
    return -1;
}

void dkm_sys_list(void) {
    printk(T, "dkm: %u modules loaded\n", g_module_count);
    for (u32 i = 0; i < g_module_count; i++) {
        printk(T, "  %s\n", g_modules[i].name);
    }
}
