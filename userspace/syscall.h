/**
 * Userspace syscall wrapper header
 * Provides convenient functions to call kernel syscalls
 */

#ifndef __USERSPACE_SYSCALL_H__
#define __USERSPACE_SYSCALL_H__

#define SYS_READ  0
#define SYS_WRITE 1
#define SYS_EXIT  60
#define SYS_PRINTK 4

/**
 * Syscall wrapper - calls int 0x80
 * eax = syscall number
 * ebx = arg1
 * ecx = arg2
 * edx = arg3
 * Returns value in eax
 */
static inline int syscall(int number, int arg1, int arg2, int arg3) {
    int result;
    __asm__ __volatile__(
        "int $0x80"
        : "=a" (result)
        : "a" (number), "b" (arg1), "c" (arg2), "d" (arg3)
        : "memory"
    );
    return result;
}

/**
 * Read from stdin
 */
static inline int read(int fd, char *buf, unsigned int count) {
    return syscall(SYS_READ, fd, (int)buf, count);
}

/**
 * Write to stdout/stderr
 */
static inline int write(int fd, const char *buf, unsigned int count) {
    return syscall(SYS_WRITE, fd, (int)buf, count);
}

/**
 * Exit program
 */
static inline void exit(int code) {
    syscall(SYS_EXIT, code, 0, 0);
    /* Should not return, but loop if it does */
    while (1) {}
}

#endif /* __USERSPACE_SYSCALL_H__ */
