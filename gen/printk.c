/**
 * Ark kernel printk implementation for dual-output (VGA + serial)
 *
 * Supports %s, %c, %d, %u, %x, %X, %%
 * Prints to both VGA memory (0xB8000) and serial COM1.
 */

 #include <stdarg.h>
// #include <stdbool.h>
 #include "ark/types.h"
 #include "ark/printk.h"
 
 /* --- COM1 serial --- */
 #define COM1 0x3F8
 
 static inline void outb(u16 port, u8 val) {
     __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
 }
 
 static inline u8 inb(u16 port) {
     u8 ret;
     __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
     return ret;
 }
 
 void serial_init(void) {
     outb(COM1 + 1, 0x00);
     outb(COM1 + 3, 0x80);
     outb(COM1 + 0, 0x03);
     outb(COM1 + 1, 0x00);
     outb(COM1 + 3, 0x03);
     outb(COM1 + 2, 0xC7);
     outb(COM1 + 4, 0x0B);
 }
 
 static void serial_wait(void) {
     while (!(inb(COM1 + 5) & 0x20));
 }
 
 static void serial_putc(char c) {
     if (c == '\n') serial_putc('\r');
     serial_wait();
     outb(COM1, c);
 }
 
 /* --- VGA --- */
 #define VGA_WIDTH   80
 #define VGA_HEIGHT  25
 #define VGA_ATTR    0x0F
 
 static volatile u16 *const vga_buf = (volatile u16 *)0xB8000;
 static u32 vga_row = 0;
 static u32 vga_col = 0;
 
 static void vga_put_at(u32 row, u32 col, char c, u8 attr) {
     if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
     vga_buf[row * VGA_WIDTH + col] = ((u16)attr << 8) | (u8)c;
 }
 
 static void vga_scroll(void) {
     for (u32 r = 1; r < VGA_HEIGHT; ++r)
         for (u32 c = 0; c < VGA_WIDTH; ++c)
             vga_buf[(r - 1) * VGA_WIDTH + c] = vga_buf[r * VGA_WIDTH + c];
     for (u32 c = 0; c < VGA_WIDTH; ++c)
         vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = ((u16)VGA_ATTR << 8) | ' ';
     if (vga_row > 0) vga_row--;
 }
 
 static void vga_putc(char c) {
     if (c == '\n') { vga_col = 0; vga_row++; }
     else if (c == '\r') { vga_col = 0; }
     else { vga_put_at(vga_row, vga_col, c, VGA_ATTR); vga_col++; }
 
     if (vga_col >= VGA_WIDTH) { vga_col = 0; vga_row++; }
     if (vga_row >= VGA_HEIGHT) vga_scroll();
 }
 
 /* --- Unified putc --- */
 static void putc(char c) {
     vga_putc(c);       // always print to VGA
     serial_putc(c);    // always print to serial
 }
 
 /* --- helpers --- */
 static void print_string(const char *s) {
     if (!s) s = "(null)";
     while (*s) putc(*s++);
 }
 
 static void print_uint(u32 v, u32 base, bool uppercase) {
     char buf[32];
     const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
     int i = 0;
     if (v == 0) { putc('0'); return; }
     while (v && i < (int)sizeof(buf)) { buf[i++] = digits[v % base]; v /= base; }
     while (i--) putc(buf[i]);
 }
 
 static void print_int(int v) {
     if (v < 0) { putc('-'); print_uint((u32)(-v), 10, false); }
     else print_uint((u32)v, 10, false);
 }
 
 /* --- Kernel printk --- */
 int printk(const char *fmt, ...) {
     va_list ap;
     int count = 0;
     va_start(ap, fmt);
 
     while (*fmt) {
         if (*fmt != '%') { putc(*fmt++); count++; continue; }
         fmt++;
         char c = *fmt++;
         switch (c) {
             case '%': putc('%'); count++; break;
             case 'c': { char ch = (char)va_arg(ap, int); putc(ch); count++; break; }
             case 's': { const char *s = va_arg(ap, const char *); print_string(s); break; }
             case 'd':
             case 'i': { int v = va_arg(ap, int); print_int(v); break; }
             case 'u': { unsigned int v = va_arg(ap, unsigned int); print_uint(v, 10, false); break; }
             case 'x': { unsigned int v = va_arg(ap, unsigned int); print_uint(v, 16, false); break; }
             case 'X': { unsigned int v = va_arg(ap, unsigned int); print_uint(v, 16, true); break; }
             default: putc('%'); putc(c); count += 2; break;
         }
     }
 
     va_end(ap);
     return count;
 }
 