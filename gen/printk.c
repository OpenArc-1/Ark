/**
 * Ark kernel printk implementation.
 *
 * Very small, freestanding formatted printer that supports:
 *   %s, %c, %d, %u, %x, %X, %%
 *
 * Output is sent to a simple VGA text console at 0xB8000 so we have
 * a very robust, QEMU-friendly early boot path.
 */

#include <stdarg.h>

#include "ark/types.h"
#include "ark/printk.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_ATTR    0x0F  /* white on black */

static volatile u16 *const vga_buf = (volatile u16 *)0xB8000;
static u32 vga_row = 0;
static u32 vga_col = 0;

static void vga_put_at(u32 row, u32 col, char c, u8 attr) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) {
        return;
    }
    u16 cell = ((u16)attr << 8) | (u8)c;
    vga_buf[row * VGA_WIDTH + col] = cell;
}

static void vga_scroll(void) {
    for (u32 r = 1; r < VGA_HEIGHT; ++r) {
        for (u32 c = 0; c < VGA_WIDTH; ++c) {
            vga_buf[(r - 1) * VGA_WIDTH + c] = vga_buf[r * VGA_WIDTH + c];
        }
    }
    for (u32 c = 0; c < VGA_WIDTH; ++c) {
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = ((u16)VGA_ATTR << 8) | ' ';
    }
    if (vga_row > 0) {
        vga_row--;
    }
}

static void putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else {
        vga_put_at(vga_row, vga_col, c, VGA_ATTR);
        vga_col++;
    }

    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
    }
}

static void print_string(const char *s) {
    if (!s) {
        s = "(null)";
    }
    while (*s) {
        putc(*s++);
    }
}

/* Use 32-bit arithmetic to avoid pulling in libgcc 64-bit helpers. */
static void print_uint(u32 v, u32 base, bool uppercase) {
    char buf[32];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (v == 0) {
        putc('0');
        return;
    }

    while (v && i < (int)sizeof(buf)) {
        buf[i++] = digits[v % base];
        v /= base;
    }

    while (i--) {
        putc(buf[i]);
    }
}

static void print_int(int v) {
    if (v < 0) {
        putc('-');
        print_uint((u32)(-v), 10, false);
    } else {
        print_uint((u32)v, 10, false);
    }
}

int printk(const char *fmt, ...) {
    va_list ap;
    int count = 0;

    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            putc(*fmt++);
            count++;
            continue;
        }

        fmt++; /* skip '%' */
        char c = *fmt++;
        switch (c) {
        case '%':
            putc('%');
            count++;
            break;
        case 'c': {
            char ch = (char)va_arg(ap, int);
            putc(ch);
            count++;
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            print_string(s);
            break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            print_int(v);
            break;
        }
        case 'u': {
            unsigned int v = va_arg(ap, unsigned int);
            print_uint(v, 10, false);
            break;
        }
        case 'x': {
            unsigned int v = va_arg(ap, unsigned int);
            print_uint(v, 16, false);
            break;
        }
        case 'X': {
            unsigned int v = va_arg(ap, unsigned int);
            print_uint(v, 16, true);
            break;
        }
        default:
            /* Unknown format specifier, print it raw. */
            putc('%');
            putc(c);
            count += 2;
            break;
        }
    }

    va_end(ap);
    return count;
}

