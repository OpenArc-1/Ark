/**
 * gen/syscall.c — Ark kernel syscall dispatcher
 *
 * Two calling conventions:
 *
 *  1. x86_64 native SYSCALL (fast path, LSTAR MSR):
 *       rax=nr, rdi=a1, rsi=a2, rdx=a3, r10=a4
 *        x86_64 ABI numbers.
 *       Handler: syscall64_dispatch()
 *
 *  2.  i386 compat (int 0x80, IDT DPL-3 gate):
 *       eax=nr, ebx=a1, ecx=a2, edx=a3
 *        i386 ABI numbers + Ark-specific numbers.
 *       Handler: syscall_dispatch()
 *
 * NOTE: Ark graphics syscalls (SYS_GFX_*) use numbers 10-16 which
 * collide with  x86_64 ABI (10=mprotect, 11=munmap, 12=brk…).
 * They are therefore only handled in the i386 compat path (int 0x80).
 * x86_64 userspace should use Ark-specific numbers >= 300 for graphics.
 */

#include "../include/ark/types.h"

#include "ark/printk.h"
#include "ark/input.h"
#include "ark/vfs.h"
#include "ark/syscall.h"

/* ──  x86_64 syscall numbers ───────────────────────────────────────── */
#define __NR64_read          0
#define __NR64_write         1
#define __NR64_open          2
#define __NR64_close         3
#define __NR64_lseek         8
#define __NR64_mmap          9
#define __NR64_brk          12
#define __NR64_getpid       39
#define __NR64_exit         60
#define __NR64_exit_group  231

/* ──  i386 compat numbers (int 0x80) ───────────────────────────────── */
#define __NR32_exit          1
#define __NR32_read          3
#define __NR32_write         4
#define __NR32_open          5
#define __NR32_close         6
#define __NR32_getpid       20
#define __NR32_brk          45
#define __NR32_exit_group  252

/* ── Ark net syscall numbers ────────────────────────────────────────────── */
#define SYS_NET_SEND     320   /* send raw ethernet frame: buf, len → 0 or -1  */
#define SYS_NET_RECV     321   /* recv raw ethernet frame: buf, maxlen → bytes  */
#define SYS_NET_MAC      322   /* get MAC address: mac[6] buf → 0 or -1        */
#define SYS_NET_GETIP    323   /* get current IP config: ip,mask,gw u32 ptrs   */
#define SYS_NET_SETIP    324   /* set static IP: ip, mask, gw as u32           */

/* ── Ark graphics syscall numbers (i386 compat path only, int 0x80) ─────── */
#define ARK_SYS_GFX_CREATE_WINDOW   10
#define ARK_SYS_GFX_DESTROY_WINDOW  11
#define ARK_SYS_GFX_DRAW_RECT       12
#define ARK_SYS_GFX_DRAW_TEXT       13
#define ARK_SYS_GFX_DRAW_LINE       14
#define ARK_SYS_GFX_CLEAR           15
#define ARK_SYS_GFX_PRESENT         16

/* ── Program break ───────────────────────────────────────────────────────── */
#define BRK_START  0x10000000ULL
#define BRK_END    0x18000000ULL
static u64 g_current_brk = BRK_START;

/* ── Exit jump buffer ────────────────────────────────────────────────────── */
typedef long jmp_buf_type[8];
static jmp_buf_type g_exit_jmp_buf;
static u8  g_exit_jmp_valid;
int        g_syscall_exit_code;

void syscall_set_exit_jmp_buf(void *buf) {
    if (!buf) { g_exit_jmp_valid = 0; return; }
    for (int i = 0; i < 8; i++)
        ((long *)g_exit_jmp_buf)[i] = ((long *)buf)[i];
    g_exit_jmp_valid = 1;
}

void syscall_do_exit(int code) {
    g_syscall_exit_code = code;
    if (g_exit_jmp_valid)
        __builtin_longjmp(g_exit_jmp_buf, 1);
}

/* ── Internal I/O helpers ────────────────────────────────────────────────── */
static u64 do_write(u64 fd, const char *buf, u64 count) {
    if (!buf || count == 0) return (u64)-1;
    if (fd == 1 || fd == 2) {
        for (u64 i = 0; i < count && i < 65536; i++) {
            char c = buf[i];
            if (!c) break;
            printk("%c", c);
        }
        return count;
    }
    int r = vfs_write((int)fd, buf, (u32)count);
    return (u64)r;
}

static u64 do_read(u64 fd, char *buf, u64 count) {
    if (!buf || count == 0) return (u64)-1;
    if (fd == 0) {
        /* Blocking line-buffered stdin — unified input path.
         *
         * Uses input_poll() which drains both PS/2 and USB HID into a
         * shared ring buffer, so USB keyboards work here too.
         *
         * We first drain any stale bytes that accumulated since boot
         * (PS/2 ACKs, QEMU startup noise) by clearing the PS/2 OBF
         * and the input ring buffer, then spin on input_poll().
         */
        extern void  input_poll(void);
        extern bool  input_has_key(void);
        extern char  input_getc(void);
        extern int   kbd_poll(void);
        extern bool  kbd_has_input(void);
        extern char  kbd_getc(void);
        extern volatile int kbd_irq_pending;

        /* Drain PS/2 OBF and software ring buffers */
        {
            volatile int drain;
            for (drain = 0; drain < 32; drain++) {
                u8 st;
                __asm__ __volatile__("inb $0x64, %0" : "=a"(st));
                if (!(st & 0x01)) break;
                u8 dummy;
                __asm__ __volatile__("inb $0x60, %0" : "=a"(dummy));
                (void)dummy;
            }
            while (kbd_has_input()) kbd_getc();
        }

        u32 n = 0;
        while (n < (u32)count - 1) {
            /* Poll all input sources (PS/2 + USB HID).
             * If nothing yet, HLT briefly so we don't busy-burn the CPU.
             * IRQ0 (timer 100 Hz) will wake us frequently enough that USB
             * poll latency is imperceptible (<10 ms). */
            __asm__ __volatile__("sti");
            input_poll();
            if (!input_has_key()) {
                kbd_irq_pending = 0;
                __asm__ __volatile__("hlt");
                kbd_irq_pending = 0;
                input_poll();
            }

            /* Consume characters */
            while (input_has_key() && n < (u32)count - 1) {
                char c = input_getc();
                if (c == '\r' || c == '\n') {
                    printk("\n");
                    buf[n] = '\0';
                    return (u64)n;
                }
                if (c == '\b' || c == 127) {
                    if (n > 0) { n--; printk("\b \b"); }
                    continue;
                }
                if (c >= 32 && c < 127) {
                    buf[n++] = c;
                    printk("%c", c);
                }
            }
        }
        buf[n] = '\0';
        return (u64)n;
    }
    int r = vfs_read((int)fd, buf, (u32)count);
    return (u64)r;
}

static u64 do_mmap(u64 addr, u64 len) {
    if (!addr) {
        u64 result = g_current_brk;
        g_current_brk = (g_current_brk + len + 0xFFF) & ~(u64)0xFFF;
        if (g_current_brk > BRK_END) return (u64)-12; /* ENOMEM */
        return result;
    }
    return addr;
}

/* ==========================================================================
 * syscall64_dispatch — x86_64 native ABI (SYSCALL instruction, LSTAR)
 *
 * Uses  x86_64 syscall numbers exclusively.
 * Ark-specific calls use numbers >= 300 to avoid any  ABI conflict.
 * Graphics stubs (10-16) are NOT handled here — they clash with brk(12).
 * ==========================================================================
 */
u64 syscall64_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a5; (void)a6;

    switch (nr) {
    /* Standard POSIX I/O */
    case __NR64_read:
        return do_read(a1, (char *)(usize)a2, a3);
    case __NR64_write:
        return do_write(a1, (const char *)(usize)a2, a3);
    case __NR64_open:
        return (u64)(long)vfs_open((const char *)(usize)a1);
    case __NR64_close:
        return (vfs_close((int)a1) < 0) ? (u64)-1 : 0;
    case __NR64_lseek:
        return (u64)-1;

    /* Memory */
    case __NR64_brk:
        if (!a1) return g_current_brk;
        if (a1 >= BRK_START && a1 <= BRK_END) g_current_brk = a1;
        return g_current_brk;
    case __NR64_mmap:
        return do_mmap(a1, a2);

    /* Process */
    case __NR64_getpid:
        return 1;
    case __NR64_exit:
    case __NR64_exit_group:
        syscall_do_exit((int)a1);
        return 0;

    /* Ark-specific (>= 300, no  conflict) */
    case 300: {
        extern long ark_syscall_fb_info(u32, u32, u32, u32);
        return (u64)ark_syscall_fb_info((u32)a1, (u32)a2, (u32)a3, 0);
    }
    case 301: {
        extern long ark_syscall_mouse_read(void);
        return (u64)ark_syscall_mouse_read();
    }
    case 0x400: {
        extern u32 *display_get_framebuffer(void);
        return (u64)(usize)display_get_framebuffer();
    }
    case SYS_DKM_LOAD: {
        extern u32 dkm_syscall_load(const char *);
        return dkm_syscall_load((const char *)(usize)a1);
    }
    case SYS_DKM_UNLOAD: {
        extern u32 dkm_syscall_unload(const char *);
        return dkm_syscall_unload((const char *)(usize)a1);
    }
    case SYS_DKM_LIST: {
        extern u32 dkm_syscall_list(void);
        return dkm_syscall_list();
    }

    default:
        return (u64)-38; /* ENOSYS */
    }
}

/* ==========================================================================
 * syscall_dispatch — i386 compat ABI (int 0x80)
 *
 * Handles  i386 numbers AND Ark-specific numbers (10-16 graphics,
 * 300-312 Ark extensions).  Called from exception64_handler on vector 0x80.
 * ==========================================================================
 */
u32 syscall_dispatch(u32 number, u32 arg1, u32 arg2, u32 arg3) {
    switch (number) {
    case 0:                        /* legacy Ark SYS_READ */
    case __NR32_read:
        return (u32)do_read(arg1, (char *)(usize)arg2, arg3);
    case __NR32_write:
        return (u32)do_write(arg1, (const char *)(usize)arg2, arg3);
    case __NR32_open:
        return (u32)(int)vfs_open((const char *)(usize)arg1);
    case __NR32_close:
        return (vfs_close((int)arg1) < 0) ? (u32)-1 : 0;
    case __NR32_getpid:
        return 1;
    case __NR32_brk:
        if (!arg1) return (u32)g_current_brk;
        if ((u64)arg1 >= BRK_START && (u64)arg1 <= BRK_END) g_current_brk = arg1;
        return (u32)g_current_brk;
    case __NR32_exit:
    case 60:
    case __NR32_exit_group:
        syscall_do_exit((int)arg1);
        return 0;

    /* Ark graphics display-server stubs */
    case ARK_SYS_GFX_CREATE_WINDOW:
    case ARK_SYS_GFX_DESTROY_WINDOW:
    case ARK_SYS_GFX_DRAW_RECT:
    case ARK_SYS_GFX_DRAW_TEXT:
    case ARK_SYS_GFX_DRAW_LINE:
    case ARK_SYS_GFX_CLEAR:
    case ARK_SYS_GFX_PRESENT:
        return 0;

    /* Ark-specific extensions */
    case 300: {
        extern long ark_syscall_fb_info(u32, u32, u32, u32);
        return (u32)ark_syscall_fb_info(arg1, arg2, arg3, 0);
    }
    case 301: {
        extern long ark_syscall_mouse_read(void);
        return (u32)ark_syscall_mouse_read();
    }
    case 0x400: {
        extern u32 *display_get_framebuffer(void);
        return (u32)(usize)display_get_framebuffer();
    }
    case SYS_DKM_LOAD: {
        extern u32 dkm_syscall_load(const char *);
        return dkm_syscall_load((const char *)(usize)arg1);
    }
    case SYS_DKM_UNLOAD: {
        extern u32 dkm_syscall_unload(const char *);
        return dkm_syscall_unload((const char *)(usize)arg1);
    }
    case SYS_DKM_LIST: {
        extern u32 dkm_syscall_list(void);
        return dkm_syscall_list();
    }

    /* ── Network ─────────────────────────────────────────────────────── */
    case SYS_NET_SEND: {
        extern int net_send(const void *buf, unsigned int len);
        return (u32)(int)net_send((const void *)(usize)arg1, (unsigned int)arg2);
    }
    case SYS_NET_RECV: {
        extern int net_recv(void *buf, unsigned int maxlen);
        return (u32)(int)net_recv((void *)(usize)arg1, (unsigned int)arg2);
    }
    case SYS_NET_MAC: {
        extern int net_get_mac(u8 mac[6]);
        return (u32)(int)net_get_mac((u8 *)(usize)arg1);
    }
    case SYS_NET_GETIP: {
        extern unsigned char g_net_config[];  /* net_config_t */
        /* g_net_config layout: local_ip(4), netmask(4), gateway(4), dns(4), mac(6), configured(4) */
        /* We return ip,mask,gw as u32 into three u32 pointers */
        u32 *ip_out   = (u32 *)(usize)arg1;
        u32 *mask_out = (u32 *)(usize)arg2;
        u32 *gw_out   = (u32 *)(usize)arg3;
        /* net_config_t: ip_addr_t is 4 u8 fields = 4 bytes each */
        extern u32 ip_to_uint32(void *ip);
        /* Simpler: just expose the raw bytes */
        extern void *get_net_config_ptr(void);
        unsigned char *cfg = (unsigned char *)get_net_config_ptr();
        if (ip_out)   *ip_out   = ((u32)cfg[0]<<24)|((u32)cfg[1]<<16)|((u32)cfg[2]<<8)|cfg[3];
        if (mask_out) *mask_out = ((u32)cfg[4]<<24)|((u32)cfg[5]<<16)|((u32)cfg[6]<<8)|cfg[7];
        if (gw_out)   *gw_out   = ((u32)cfg[8]<<24)|((u32)cfg[9]<<16)|((u32)cfg[10]<<8)|cfg[11];
        return 0;
    }
    case SYS_NET_SETIP: {
        /* arg1=ip, arg2=mask, arg3=gw — all host-byte-order u32 */
        /* ip_addr_t is { u8 a,b,c,d } — matches 4 bytes */
        typedef struct { u8 a,b,c,d; } sc_ip_t;
        extern void ip_set_static(sc_ip_t ip, sc_ip_t mask, sc_ip_t gw);
        sc_ip_t ip   = {(u8)(arg1>>24),(u8)(arg1>>16),(u8)(arg1>>8),(u8)(arg1)};
        sc_ip_t mask = {(u8)(arg2>>24),(u8)(arg2>>16),(u8)(arg2>>8),(u8)(arg2)};
        sc_ip_t gw   = {(u8)(arg3>>24),(u8)(arg3>>16),(u8)(arg3>>8),(u8)(arg3)};
        ip_set_static(ip, mask, gw);
        return 0;
    }

    default:
        return (u32)-1;
    }
}
