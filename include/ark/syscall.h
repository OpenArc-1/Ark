/**
 * ark/syscall.h — Ark i386 syscall ABI
 *
 * Uses int 0x80 (i386 Linux-compatible syscall gate).
 * Ark's kernel handles int 0x80 in gen/syscall.c / arch/x86/int80.S.
 *
 * i386 int 0x80 calling convention:
 *   eax = syscall number
 *   ebx = arg1,  ecx = arg2,  edx = arg3
 *   esi = arg4,  edi = arg5
 *   return value in eax (negative = error)
 */
#ifndef ARK_SYSCALL_H
#define ARK_SYSCALL_H

/* ── i386 syscall numbers (int 0x80) ────────────────────────────────────── */
#define __NR_exit             1
#define __NR_read             3
#define __NR_write            4
#define __NR_open             5
#define __NR_close            6
#define __NR_lseek           19
#define __NR_getpid          20
#define __NR_brk             45
#define __NR_mmap2          192
#define __NR_exit_group     252

/* ── Ark-specific syscall numbers ───────────────────────────────────────── */
#define SYS_FB_INFO         300
#define SYS_MOUSE_READ      301
#define SYS_GET_FRAMEBUFFER 0x400

#define SYS_DKM_LOAD        310
#define SYS_DKM_UNLOAD      311
#define SYS_DKM_LIST        312

/* Unified keyboard syscalls (PS/2 + USB, routed through gen/input.c) */
#define SYS_KBD_POLL        330  /* non-blocking: returns next scan-code or 0   */
#define SYS_KBD_KEY_STATE   331  /* arg1=scancode -> 1 if held, 0 if not        */
#define SYS_KBD_MODIFIERS   332  /* returns ARK_MOD_* bitmask of live modifiers */

/* Graphics display-server syscalls */
#define SYS_GFX_CREATE_WINDOW   10
#define SYS_GFX_DESTROY_WINDOW  11
#define SYS_GFX_DRAW_RECT       12
#define SYS_GFX_DRAW_TEXT       13
#define SYS_GFX_DRAW_LINE       14
#define SYS_GFX_CLEAR           15
#define SYS_GFX_PRESENT         16

/* ── int 0x80 wrappers ───────────────────────────────────────────────────── */

static inline long syscall0(long n) {
    long r;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(r)
        : "a"(n)
        : "memory"
    );
    return r;
}

static inline long syscall1(long n, long a1) {
    long r;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a1)
        : "memory"
    );
    return r;
}

static inline long syscall2(long n, long a1, long a2) {
    long r;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a1), "c"(a2)
        : "memory"
    );
    return r;
}

static inline long syscall3(long n, long a1, long a2, long a3) {
    long r;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return r;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4) {
    long r;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory"
    );
    return r;
}

static inline long syscall6(long n, long a1, long a2, long a3,
                             long a4, long a5, long a6) {
    /* i386 int 0x80 only has 5 arg registers; push a6 on stack if needed.
     * Ark doesn't currently use 6-arg syscalls from userspace, but keep
     * the prototype compatible. a6 is silently ignored here. */
    (void)a6;
    long r;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(r)
        : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory"
    );
    return r;
}

/* Generic 3-arg shim used by older Ark code */
static inline long syscall(long number, long arg1, long arg2, long arg3) {
    return syscall3(number, arg1, arg2, arg3);
}

/* ── Named POSIX-ish helpers ─────────────────────────────────────────────── */

static inline long read(int fd, void *buf, unsigned long n) {
    return syscall3(__NR_read, (long)fd, (long)buf, (long)n);
}
static inline long write(int fd, const void *buf, unsigned long n) {
    return syscall3(__NR_write, (long)fd, (long)buf, (long)n);
}
static inline int open(const char *path, int flags, int mode) {
    (void)mode;
    return (int)syscall3(__NR_open, (long)path, (long)flags, 0);
}
static inline int close(int fd) {
    return (int)syscall1(__NR_close, (long)fd);
}
static inline void *brk(void *addr) {
    return (void *)syscall1(__NR_brk, (long)addr);
}
static inline long getpid(void) {
    return syscall0(__NR_getpid);
}

/* mmap wrapper (uses mmap2 on i386: offset in 4096-byte pages) */
static inline void *mmap_anon(void *hint, unsigned long len) {
    /* prot=PROT_READ|PROT_WRITE=3, flags=MAP_PRIVATE|MAP_ANONYMOUS=0x22 */
    return (void *)syscall6(__NR_mmap2, (long)hint, (long)len,
                            3L, 0x22L, -1L, 0L);
}

#ifndef ARK_STDLIB_EXIT   /* stdlib.c provides its own exit() */
__attribute__((noreturn))
static inline void exit(int code) {
    syscall1(__NR_exit_group, (long)code);
    __builtin_unreachable();
}
#endif

/* ── Ark network syscalls ───────────────────────────────────────────────── */
#define SYS_NET_SEND   320
#define SYS_NET_RECV   321
#define SYS_NET_MAC    322
#define SYS_NET_GETIP  323
#define SYS_NET_SETIP  324

static inline int ark_net_send(const void *buf, unsigned int len) {
    return (int)syscall2(SYS_NET_SEND, (long)buf, (long)len);
}
static inline int ark_net_recv(void *buf, unsigned int maxlen) {
    return (int)syscall2(SYS_NET_RECV, (long)buf, (long)maxlen);
}
static inline int ark_net_get_mac(unsigned char mac[6]) {
    return (int)syscall1(SYS_NET_MAC, (long)mac);
}
static inline int ark_net_get_ip(unsigned int *ip, unsigned int *mask, unsigned int *gw) {
    return (int)syscall3(SYS_NET_GETIP, (long)ip, (long)mask, (long)gw);
}
static inline int ark_net_set_ip(unsigned int ip, unsigned int mask, unsigned int gw) {
    return (int)syscall3(SYS_NET_SETIP, (long)ip, (long)mask, (long)gw);
}

/* ── Ark-specific helpers ────────────────────────────────────────────────── */

static inline unsigned long get_framebuffer(void) {
    return (unsigned long)syscall0(SYS_GET_FRAMEBUFFER);
}

static inline long fb_info(unsigned int *w, unsigned int *h, unsigned int *pitch) {
    return syscall3(SYS_FB_INFO, (long)w, (long)h, (long)pitch);
}

static inline long mouse_read(void) {
    return syscall0(SYS_MOUSE_READ);
}

/* Graphics */
static inline int gfx_create_window(int x, int y, int width, int height) {
    return (int)syscall4(SYS_GFX_CREATE_WINDOW,
                         (long)((x << 16) | y), (long)((width << 16) | height),
                         0L, 0L);
}
static inline int gfx_clear(int win_id, int color) {
    return (int)syscall3(SYS_GFX_CLEAR, (long)win_id, (long)color, 0L);
}
static inline int gfx_present(void) {
    return (int)syscall0(SYS_GFX_PRESENT);
}

#endif /* ARK_SYSCALL_H */