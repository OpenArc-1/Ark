/**
 * init_api.c - exports the kernel API table for init.bin
 */

#include "ark/init_api.h"
#include "ark/printk.h"
#include "ark/input.h"
#include "ark/time.h"
#include "ark/vfs.h"
#include "ark/tty.h"

extern void ark_cpuid(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
extern void ark_get_cpu_vendor(char out_13[13]);

static const ark_kernel_api_t g_kernel_api = {
    .version = ARK_INIT_API_VERSION,
    .printk = printk,
    .input_has_key = input_has_key,
    .input_getc = input_getc,
    .input_read = input_read,
    .read_rtc = read_rtc,
    .cpuid = ark_cpuid,
    .get_cpu_vendor = ark_get_cpu_vendor,
    .vfs_open = vfs_open,
    .vfs_read = vfs_read,
    .vfs_close = vfs_close,
    .vfs_file_size = vfs_file_size,
    .vfs_file_exists = vfs_file_exists,
    .vfs_list_count = vfs_list_count,
    .vfs_list_at = vfs_list_at,
    .tty_alloc = tty_alloc,
    .tty_free = tty_free,
    .tty_current = tty_current,
    .tty_switch = tty_switch,
    .tty_get_name = tty_get_name,
    .tty_valid = tty_valid,
    .tty_debug = tty_debug,
};

const ark_kernel_api_t *ark_kernel_api(void) {
    return &g_kernel_api;
}

