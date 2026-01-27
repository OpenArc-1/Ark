#ifndef __USERSPACE_SYSCALL_H__
#define __USERSPACE_SYSCALL_H__

/* Ark syscall numbers */
#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_PRINTK  3

static inline int syscall(int number, int arg1, int arg2, int arg3) {
    int result;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return result;
}

static inline int read(int fd, char *buf, int count) {
    return syscall(SYS_READ, fd, (int)buf, count);
}

static inline int write(int fd, const char *buf, int count) {
    return syscall(SYS_WRITE, fd, (int)buf, count);
}

static inline void printk(const char *str) {
    syscall(SYS_PRINTK, (int)str, 0, 0);
}

static inline void exit(int code) {
    syscall(SYS_EXIT, code, 0, 0);
    while (1) { __asm__ __volatile__("hlt"); }
}

#endif
