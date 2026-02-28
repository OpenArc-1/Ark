/**
 * init_api.h - Kernel API table exposed to init.bin
 *
 * Ark runs init.bin in the same address space today (ring0 jump),
 * so this API is a stable, versioned way for init.bin to call into
 * kernel facilities without reaching into internal symbols.
 */

#pragma once

#include "ark/types.h"
#include "ark/time.h"

#define ARK_INIT_API_VERSION 3  /* bump for new log APIs */

/* device node types for vfs_mknod */
#define ARK_DEV_BLK  1   /* block device (sda)      */
#define ARK_DEV_CHR  2   /* char device  (kbd, nic) */

/* major numbers — keep in sync with your driver table */
#define ARK_MAJ_SDA      8
#define ARK_MAJ_PS2KBD   11
#define ARK_MAJ_USBKBD   12
#define ARK_MAJ_E1000    13

typedef struct ark_kernel_api {
    u32 version; /* ARK_INIT_API_VERSION */

    /* Logging */
    int (*printk)(const char *fmt, ...);
    /* optional kernel log capture - writes into a ramfs file */
    void (*log_open)(const char *path);
    void (*log_write)(const char *buf, u32 len);
    void (*log_close)(void);

    /* Input */
    bool (*input_has_key)(void);
    char (*input_getc)(void); /* blocks */
    void (*input_read)(char *buffer, int max_len, bool hide_input);

    /* Time */
    rtc_time_t (*read_rtc)(void);

    /* CPU */
    void (*cpuid)(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
    void (*get_cpu_vendor)(char out_13[13]); /* NUL-terminated */

    /* VFS - file ops */
    int  (*vfs_open)(const char *path);
    int  (*vfs_read)(int fd, void *buf, u32 size);
    int  (*vfs_close)(int fd);
    u32  (*vfs_file_size)(int fd);
    u8   (*vfs_file_exists)(const char *path);
    u32  (*vfs_list_count)(const char *path);
    u8   (*vfs_list_at)(const char *path, u32 index, char *name_out, u32 name_max);

    /* VFS - directory + device node creation */
    int  (*vfs_mkdir)(const char *path);
    int  (*vfs_mknod)(const char *path, u32 type, u32 major, u32 minor);

    /* TTY / sessions */
    u32  (*tty_alloc)(void);
    void (*tty_free)(u32 sid);
    u32  (*tty_current)(void);
    void (*tty_switch)(u32 sid);
    void (*tty_get_name)(u32 sid, char *out, u32 max);
    u8   (*tty_valid)(u32 sid);
    void (*tty_debug)(u32 sid, const char *fmt, ...);

    /* Hardware presence flags - kernel fills these at boot */
    u8 has_usb_kbd;   /* 1 if USB keyboard was enumerated        */
    u8 has_e1000;     /* 1 if Intel e1000 NIC found on PCI bus   */
    int (*printc)(u8 color, const char*fmt, ...);

    /* When kernel runs /init as a script (#!/bin/sh), it runs the interpreter
       and sets this so the interpreter can open and execute the script path. */
    const char *(*get_startup_script_path)(void);
} ark_kernel_api_t;

/* Called by kernel before executing the shebang interpreter (e.g. /bin/sh) */
void ark_set_startup_script_path(const char *path);

/*
 * init.bin entrypoint signature.
 * The kernel will call ELF e_entry as: int entry(const ark_kernel_api_t *api)
 */
typedef int (*ark_init_entry_t)(const ark_kernel_api_t *api);

/* Global kernel API table (for init.bin and scripts) */
const ark_kernel_api_t *ark_kernel_api(void);