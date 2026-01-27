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
 #include <stdbool.h>
 
 #define ARK_INIT_API_VERSION 2
 
 typedef struct ark_kernel_api {
     u32 version; /* ARK_INIT_API_VERSION */
 
     /* Logging */
     int (*printk)(const char *fmt, ...);
 
     /* Input */
     bool (*input_has_key)(void);
     char (*input_getc)(void); /* blocks */
     void (*input_read)(char *buffer, int max_len, bool hide_input);
 
     /* Time */
     rtc_time_t (*read_rtc)(void);
 
     /* CPU */
     void (*cpuid)(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
     void (*get_cpu_vendor)(char out_13[13]); /* NUL-terminated */
 
     /* VFS (real FS usage) */
     int (*vfs_open)(const char *path);
     int (*vfs_read)(int fd, void *buf, u32 size);
     int (*vfs_close)(int fd);
     u32 (*vfs_file_size)(int fd);
     u8 (*vfs_file_exists)(const char *path);
     u32 (*vfs_list_count)(const char *path);
     u8 (*vfs_list_at)(const char *path, u32 index, char *name_out, u32 name_max);
 
     /* TTY / sessions */
     u32 (*tty_alloc)(void);
     void (*tty_free)(u32 sid);
     u32 (*tty_current)(void);
     void (*tty_switch)(u32 sid);
     void (*tty_get_name)(u32 sid, char *out, u32 max);
     u8 (*tty_valid)(u32 sid);
     void (*tty_debug)(u32 sid, const char *fmt, ...);
 } ark_kernel_api_t;
 
 /*
  * init.bin entrypoint signature.
  * The kernel will call ELF e_entry as: int entry(const ark_kernel_api_t *api)
  */
 typedef int (*ark_init_entry_t)(const ark_kernel_api_t *api);

 /* Global kernel API table (for init.bin and scripts) */
 const ark_kernel_api_t *ark_kernel_api(void);

