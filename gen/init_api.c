/**
 * gen/init_api.c — exports the kernel API table for init.bin
 */
#include "ark/init_api.h"
#include "ark/printk.h"
#include "ark/input.h"
#include "ark/time.h"
#include "ark/vfs.h"
#include "ark/tty.h"
#include "ark/pci.h"
#include "ark/log.h"  /* expose log helpers to init.bin */
#include "ark/arch.h"
#include "ark/elf_loader.h"
#include "ark/ramfs.h"
#include "ark/sched.h"

/* These are real symbols defined in gen/cpu.c */
extern void ark_cpuid_sym(u32 leaf, u32 subleaf,
                          u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
extern void ark_get_cpu_vendor(char out_13[13]);

static const char *g_startup_script_path = NULL;

void ark_set_startup_script_path(const char *path) {
    g_startup_script_path = path;
}

static const char *get_startup_script_path(void) {
    return g_startup_script_path;
}

/* Forward declaration — g_kernel_api is defined below, but api_exec_elf
 * needs to pass &g_kernel_api to elf_execute. */
static ark_kernel_api_t g_kernel_api;

/*
 * api_exec_elf — kernel-side implementation of exec_elf for init.bin.
 * Reads 'path' from the VFS and executes it as an Ark init-style ELF,
 * passing the kernel API table as the first argument.
 *
 * Previously this called elf_execute_argv(data, sz, argc, argv), which
 * calls the ELF entry as entry(int argc, char **argv).  All Ark binaries
 * (arkwm, arksh, texted) expect entry(const ark_kernel_api_t *api) — the
 * init-style ABI defined by ark_init_entry_t.  With the argv path, the
 * child's `api` parameter received argc=0 (NULL), causing an immediate
 * crash or silent no-op on the very first api->... call.
 *
 * Fix: use elf_execute(data, sz, api) which calls entry(api) correctly.
 * argc/argv are intentionally unused — Ark init-style programs don't use them.
 */
static int api_exec_elf(const char *path, int argc, char **argv) {
    (void)argc; (void)argv;   /* Ark init binaries use api, not argc/argv */
    u32 sz = 0;
    u8 *data = ramfs_get_file(path, &sz);
    if (!data || sz == 0) {
        printk(T, "exec_elf: %s: not found\n", path);
        return -1;
    }
    return elf_execute(data, sz, &g_kernel_api);
}

static ark_kernel_api_t g_kernel_api = {
    .version        = ARK_INIT_API_VERSION,
    .printk         = printk,
    .input_has_key  = input_has_key,
    .input_getc     = input_getc,
    .input_read     = input_read,
    .input_get_modifiers = input_get_modifiers,
    .read_rtc       = read_rtc,
    .cpuid          = ark_cpuid_sym,
    .get_cpu_vendor = ark_get_cpu_vendor,
    .vfs_open       = vfs_open,
    .vfs_read       = vfs_read,
    .vfs_close      = vfs_close,
    .vfs_file_size  = vfs_file_size,
    .vfs_file_exists = vfs_file_exists,
    .vfs_list_count = vfs_list_count,
    .vfs_list_at    = vfs_list_at,
    .vfs_mkdir      = vfs_mkdir,
    .vfs_mknod      = vfs_mknod,
    /* logging helpers (version >= 3) */
    .log_open       = log_open,
    .log_write      = log_write,
    .log_close      = log_close,
    .tty_alloc      = tty_alloc,
    .tty_free       = tty_free,
    .tty_current    = tty_current,
    .tty_switch     = tty_switch,
    .tty_get_name   = tty_get_name,
    .tty_valid      = tty_valid,
    .tty_debug      = tty_debug,
    .printc         = printc,
    .get_startup_script_path = get_startup_script_path,
    .exec_elf            = api_exec_elf,
    .sched_list_tasks    = sched_list_tasks,
    .sched_get_stats     = (void(*)(void*))sched_get_stats,
    .sched_spawn         = sched_spawn,
    .sched_yield         = sched_yield,
    .sched_sleep         = sched_sleep,
    .sched_kill          = sched_kill,
    .sched_kill_by_name  = sched_kill_by_name,
    .sched_find_tid_by_name = sched_find_tid_by_name,
    .sched_set_affinity  = sched_set_affinity,
    .sched_block         = sched_block,
    .sched_unblock       = sched_unblock,
    .sched_current_tid   = sched_current_tid,
    .sched_cpu_thread_count = sched_cpu_thread_count,
};

const ark_kernel_api_t *ark_kernel_api(void) {
    g_kernel_api.has_usb_kbd = CONFIG_USB_ENABLE ? pci_usb_kbd_present() : 0;
    g_kernel_api.has_e1000   = CONFIG_NET_ENABLE ? pci_e1000_present() : 0;
    return &g_kernel_api;
}