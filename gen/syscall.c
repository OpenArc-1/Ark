/**
 * Syscall Handler for userspace (int 0x80)
 */

#include "../include/ark/types.h"
#include "../include/ark/userspacebuf.h"
#include "ark/printk.h"

/**
 * Shared userspace output buffer
 */
uspace_buffer_t g_uspace_buffer = {
    .buffer = {0},
    .write_pos = 0,
    .read_pos = 0,
    .activity_flag = 0
};

/**
 * Syscall: write(fd, buf, count)
 * eax = 1 (SYS_WRITE)
 * ebx = fd (1=stdout, 2=stderr)
 * ecx = buf pointer
 * edx = count
 */
u32 syscall_write(u32 fd, const char *buf, u32 count) {
    if (count == 0) return 0;
    if (buf == NULL) {
        printk("[syscall] ERROR: NULL buffer\n");
        return -1;
    }
    if (fd != 1 && fd != 2) return -1;  /* Only stdout/stderr */
    
    /* Output character by character */
    for (u32 i = 0; i < count && i < 10000; i++) {
        char c = buf[i];
        if (c == '\0') break;
        printk("%c", c);
    }
    
    return count;
}

/**
 * Main syscall dispatcher - called from int 0x80 handler
 */
u32 syscall_dispatch(u32 number, u32 arg1, u32 arg2, u32 arg3) {
    switch (number) {
        case 1:  /* SYS_WRITE */
            return syscall_write(arg1, (const char *)arg2, arg3);
        
        case 60:  /* SYS_EXIT */
            return 0;  /* Stub */
        
        default:
            return -1;
    }
}


