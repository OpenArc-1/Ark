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

static ark_kernel_api_t g_kernel_api = {
    .version        = ARK_INIT_API_VERSION,
    .printk         = printk,
    .input_has_key  = input_has_key,
    .input_getc     = input_getc,
    .input_read     = input_read,
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
};

const ark_kernel_api_t *ark_kernel_api(void) {
    g_kernel_api.has_usb_kbd = CONFIG_USB_ENABLE ? pci_usb_kbd_present() : 0;
    g_kernel_api.has_e1000   = CONFIG_NET_ENABLE ? pci_e1000_present() : 0;
    return &g_kernel_api;
}
