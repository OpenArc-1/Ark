/**
 * Userspace syscall wrapper library
 * Uses int 0x80 to invoke kernel syscalls
 */

#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "../include/ark/types.h"

#define SYS_WRITE   1
#define SYS_READ    3
#define SYS_EXIT    60

#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/**
 * Write via int 0x80 syscall
 */
static inline long write(int fd, const void *buf, unsigned long count) {
    long result;
    
    __asm__ __volatile__ (
        "int $0x80"
        : "=a" (result)
        : "a" (SYS_WRITE),      /* eax = syscall number */
          "b" (fd),              /* ebx = fd */
          "c" (buf),             /* ecx = buffer */
          "d" (count)            /* edx = count */
        : "memory"
    );
    
    return result;
}

/**
 * Simple puts - print string with newline
 */
static void uspace_puts(const char *s) {
    while (*s) {
        write(STDOUT_FILENO, s, 1);
        s++;
    }
    write(STDOUT_FILENO, "\n", 1);
}

#endif
