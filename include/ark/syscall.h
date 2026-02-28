#ifndef __USERSPACE_SYSCALL_H__
#define __USERSPACE_SYSCALL_H__

#include "types.h"

/* Ark syscall numbers */
#define SYS_READ                0
#define SYS_WRITE               1
#define SYS_EXIT                2
#define SYS_PRINTK              3
#define SYS_GET_FRAMEBUFFER     4

/* dynamic kernel module loader syscalls (ks/dkmload) */
#define SYS_DKM_LOAD            310    /* arg1 = path */
#define SYS_DKM_UNLOAD          311    /* arg1 = module name */
#define SYS_DKM_LIST            312    /* no args */

/* Display server syscalls */
#define SYS_GFX_CREATE_WINDOW   10
#define SYS_GFX_DESTROY_WINDOW  11
#define SYS_GFX_DRAW_RECT       12
#define SYS_GFX_DRAW_TEXT       13
#define SYS_GFX_DRAW_LINE       14
#define SYS_GFX_CLEAR           15
#define SYS_GFX_PRESENT         16

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

static inline void exit(int code) {
    syscall(SYS_EXIT, code, 0, 0);
    while (1) { __asm__ __volatile__("hlt"); }
}

/* Graphics syscalls */
static inline int gfx_create_window(int x, int y, int width, int height) {
    /* syscall(number, arg1, arg2, arg3) */
    /* Pack: arg1=(x<<16)|y, arg2=(width<<16)|height, arg3=0 */
    return syscall(SYS_GFX_CREATE_WINDOW, (x<<16)|y, (width<<16)|height, 0);
}

static inline u32 *get_framebuffer(void) {
    /* Special syscall to get framebuffer address */
    int result = syscall(SYS_GET_FRAMEBUFFER, 0, 0, 0);
    return (u32 *)result;
}

static inline int gfx_draw_rect(int win_id, int x, int y, int w, int h, int color) {
    /* arg1=win_id, arg2=(x<<16)|y, arg3=(w<<16)|h */
    /* color passed separately via syscall with extended args */
    return syscall(SYS_GFX_DRAW_RECT, win_id, (x<<16)|y, (w<<16)|h);
}

static inline int gfx_draw_text(int win_id, int x, int y, const char *text, int color) {
    return syscall(SYS_GFX_DRAW_TEXT, win_id, (int)text, (x<<16)|y);
}

static inline int gfx_clear(int win_id, int color) {
    return syscall(SYS_GFX_CLEAR, win_id, color, 0);
}

static inline int gfx_present(void) {
    return syscall(SYS_GFX_PRESENT, 0, 0, 0);
}

#endif