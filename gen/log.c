
/*
 * log.c - simple in‑kernel text logger that writes to a ramfs file
 *
 * Keeps a fixed-size static buffer and mirrors every character passed
 * through log_putchar() into it.  The ramfs entry is updated so that
 * userspace can open the path and read the accumulated log.
 */

#include "ark/types.h"
#include "ark/log.h"
#include "ark/ramfs.h"
#include "ark/printk.h"

#define LOG_BUFFER_SIZE (64 * 1024)  /* 64KiB for logs, adjust as needed */

static char g_log_path[RAMFS_MAX_FILENAME];
static char g_log_buf[LOG_BUFFER_SIZE];
static u32  g_log_pos = 0;
static bool g_log_active = false;

void log_open(const char *path) {
    if (!path || !*path) return;
    /* copy path and reset buffer */
    u32 i = 0;
    while (path[i] && i + 1 < sizeof(g_log_path)) {
        g_log_path[i] = path[i];
        i++;
    }
    g_log_path[i] = '\0';

    g_log_pos = 0;
    g_log_active = true;

    /* create/overwrite ramfs entry pointing at our static buffer */
    ramfs_add_file(g_log_path, (u8 *)g_log_buf, 0);
}

void log_putchar(char c) {
    if (!g_log_active) return;
    if (g_log_pos + 1 > LOG_BUFFER_SIZE) {
        /* drop character when full */
        return;
    }
    g_log_buf[g_log_pos++] = c;
    ramfs_set_file_size(g_log_path, g_log_pos);
}

void log_write(const char *data, u32 len) {
    if (!g_log_active || !data || len == 0) return;
    for (u32 i = 0; i < len; i++)
        log_putchar(data[i]);
}

void log_close(void) {
    g_log_active = false;
}
