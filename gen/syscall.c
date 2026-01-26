/**
 * Syscall Handler for userspace (int 0x80)
 */

#include "../include/ark/types.h"
#include "../include/ark/userspacebuf.h"
#include "ark/printk.h"
#include "ark/input.h"

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
 * Syscall: read(fd, buf, count)
 * eax = 0 (SYS_READ)
 * ebx = fd (0=stdin)
 * ecx = buf pointer
 * edx = count (max bytes to read)
 * Returns: number of bytes read, or -1 on error
 */
u32 syscall_read(u32 fd, char *buf, u32 count) {
    if (count == 0 || buf == NULL) return 0;
    if (fd != 0) return -1;  /* Only stdin */
    
    /* Check if input is ready */
    extern bool input_is_ready(void);
    if (!input_is_ready()) {
        return -1;  /* Input not initialized */
    }
    
    /* Use input_getc to read characters */
    extern char input_getc(void);
    
    u32 bytes_read = 0;
    for (u32 i = 0; i < count - 1; i++) {  /* Leave room for null terminator */
        char c = input_getc();  /* Blocks until character available */
        
        if (c == 0) {
            /* No input available or error */
            break;
        }
        
        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            bytes_read = i;
            break;
        } else if (c == '\b' || c == 0x7F) {
            /* Backspace handling - move back if possible */
            if (i > 0) {
                i -= 2;  /* Will be incremented back to i-1 */
                if (i < 0) i = 0;
            } else {
                i--;  /* Stay at 0 */
            }
        } else if (c >= 32 && c < 127) {
            /* Printable character */
            buf[i] = c;
            bytes_read = i + 1;
        }
        /* Ignore other control characters */
    }
    
    if (bytes_read < count) {
        buf[bytes_read] = '\0';
    }
    
    return bytes_read;
}

/**
 * Main syscall dispatcher - called from int 0x80 handler
 */
u32 syscall_dispatch(u32 number, u32 arg1, u32 arg2, u32 arg3) {
    switch (number) {
        case 0:  /* SYS_READ */
            return syscall_read(arg1, (char *)arg2, arg3);
        
        case 1:  /* SYS_WRITE */
            return syscall_write(arg1, (const char *)arg2, arg3);
        
        case 60:  /* SYS_EXIT */
            return 0;  /* Stub */
        
        default:
            return -1;
    }
}


