/**
 * Syscall Handler for userspace (int 0x80)
 * Linux i386 ABI so libc-linked binaries work: 1=exit, 3=read, 4=write,
 * 5=open, 6=close, 20=getpid, 45=brk, 252=exit_group
 */

#include "../include/ark/types.h"
#include "../include/ark/userspacebuf.h"
#include "ark/printk.h"
#include "ark/input.h"
#include "ark/vfs.h"
#include "ark/syscall.h"   /* our own syscall numbers */

/* Linux i386 syscall numbers (for libc compatibility) */
#define __NR_exit      1
#define __NR_read      3
#define __NR_write     4
#define __NR_open      5
#define __NR_close     6
#define __NR_getpid   20
#define __NR_brk      45
#define __NR_exit_group 252

/* Program break for brk() - libc malloc uses this */
#define BRK_START  0x10000000u
#define BRK_END    0x11000000u
static u32 g_current_brk = BRK_START;

/* Exit: longjmp back to ELF loader when set by elf_loader */
typedef long jmp_buf_type[6];  /* enough for i386 */
static jmp_buf_type g_exit_jmp_buf;
static u8 g_exit_jmp_valid;

int g_syscall_exit_code;  /* exit status after longjmp */

void syscall_set_exit_jmp_buf(void *buf) {
    if (!buf) { g_exit_jmp_valid = 0; return; }
    for (int i = 0; i < 6; i++) ((long *)g_exit_jmp_buf)[i] = ((long *)buf)[i];
    g_exit_jmp_valid = 1;
}

void syscall_do_exit(int code) {
    g_syscall_exit_code = code;
    if (g_exit_jmp_valid)
        __builtin_longjmp(g_exit_jmp_buf, 1);
    /* else not started from loader; ignore */
}

/**
 * Shared userspace output buffer
 */
uspace_buffer_t g_uspace_buffer = {
    .buffer = {0},
    .write_pos = 0,
    .read_pos = 0,
    .activity_flag = 0
};

static u32 syscall_write_inner(u32 fd, const char *buf, u32 count) {
    if (count == 0) return 0;
    if (buf == NULL) return (u32)-1;
    if (fd == 1 || fd == 2) {
        for (u32 i = 0; i < count && i < 10000; i++) {
            char c = buf[i];
            if (c == '\0') break;
            printk("%c", c);
        }
        return count;
    }
    /* fd >= 3: VFS (e.g. open file); vfs_write returns -1 for read-only */
    int r = vfs_write((int)fd, buf, count);
    return (u32)r;
}

static u32 syscall_read_inner(u32 fd, char *buf, u32 count) {
    if (count == 0 || buf == NULL) return (u32)-1;
    if (fd == 0) {
        extern bool input_is_ready(void);
        extern char input_getc(void);
        if (!input_is_ready()) return (u32)-1;
        u32 bytes_read = 0;
        for (u32 i = 0; i < count - 1; i++) {
            char c = input_getc();
            if (c == 0) break;
            if (c == '\n' || c == '\r') { buf[i] = '\0'; bytes_read = i; break; }
            if (c == '\b' || c == 0x7F) {
                if (i > 0) { i -= 2; if ((int)i < 0) i = 0; } else i--;
            } else if (c >= 32 && c < 127) { buf[i] = c; bytes_read = i + 1; }
        }
        if (bytes_read < count) buf[bytes_read] = '\0';
        return bytes_read;
    }
    int r = vfs_read((int)fd, buf, count);
    return (u32)r;
}

/**
 * Main syscall dispatcher - Linux i386 ABI
 */
u32 syscall_dispatch(u32 number, u32 arg1, u32 arg2, u32 arg3) {
    switch (number) {
        case 0:  /* legacy Ark SYS_READ */
            return syscall_read_inner(arg1, (char *)arg2, arg3);
        case 1:  /* __NR_exit */
            syscall_do_exit((int)arg1);
            return 0;
        case 3:  /* __NR_read */
            return syscall_read_inner(arg1, (char *)arg2, arg3);
        case 4:  /* __NR_write */
            return syscall_write_inner(arg1, (const char *)arg2, arg3);
        case 5:  /* __NR_open: path, flags, mode */
            return (u32)vfs_open((const char *)arg1);
        case 6:  /* __NR_close */
            return (u32)(vfs_close((int)arg1) < 0 ? -1 : 0);
        case 20: /* __NR_getpid */
            return 1;
        case 45: /* __NR_brk: arg1 = new brk; return current brk */
            if (arg1 == 0)
                return g_current_brk;
            if (arg1 >= BRK_START && arg1 <= BRK_END)
                g_current_brk = arg1;
            return g_current_brk;
        case 60: /* legacy / x86_64 style exit */
            syscall_do_exit((int)arg1);
            return 0;
        case 252: /* __NR_exit_group */
            syscall_do_exit((int)arg1);
            return 0;
        case 10: case 11: case 12: case 13: case 14: case 15: case 16:
            /* Graphics - display server in userspace */
            return 0;
        case 0x400: { /* Ark: SYS_GET_FRAMEBUFFER */
            extern u32 *display_get_framebuffer(void);
            u32 *fb = display_get_framebuffer();
            return (u32)fb;
        }

        /* ── VESA framebuffer info for libark userspace ─────────────────── */
        case 300: { /* SYS_FB_INFO: arg1=&w, arg2=&h, arg3=&pitch, arg4 unused */
            extern long ark_syscall_fb_info(u32, u32, u32, u32);
            /* arg4 (bpp pointer) not passed by our ABI stub — pass 0 */
            return (u32)ark_syscall_fb_info(arg1, arg2, arg3, 0);
        }

        /* ── PS/2 mouse raw packet read ─────────────────────────────────── */
        case 301: { /* SYS_MOUSE_READ: returns packed 3-byte packet or -1 */
            extern long ark_syscall_mouse_read(void);
            return (u32)ark_syscall_mouse_read();
        }

        /* ---------- dynamic kernel module loader syscalls ----------- */
        case SYS_DKM_LOAD: {
            extern u32 dkm_syscall_load(const char *path);
            return dkm_syscall_load((const char *)arg1);
        }
        case SYS_DKM_UNLOAD: {
            extern u32 dkm_syscall_unload(const char *name);
            return dkm_syscall_unload((const char *)arg1);
        }
        case SYS_DKM_LIST: {
            extern u32 dkm_syscall_list(void);
            return dkm_syscall_list();
        }

        default:
            return (u32)-1;
    }
}


