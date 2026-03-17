/**
 * printk.c — kernel framebuffer console + COM1 serial for Ark
 *
 * Key design decisions for real-hardware reliability:
 *
 *  1. SHADOW TEXT BUFFER — we never READ from the framebuffer.
 *     GPU framebuffer memory is mapped write-combining (WC) by firmware;
 *     reads from WC memory return stale/undefined data on real hardware.
 *     scroll() shifts the shadow cell array and redraws from that,
 *     so the framebuffer is write-only throughout.
 *
 *  2. CORRECT PIXEL ADDRESSING — draw_glyph uses byte arithmetic:
 *       u32 *row = (u32*)(g_fb + y*pitch + x*4)
 *     NOT (u32*)(g_fb + y*pitch) + x  [which doubles the x offset].
 *
 *  3. PITCH SANITY — some GRUB versions report pitch in pixels not bytes
 *     for 32-bpp modes; detected and fixed at set_fb time.
 *
 *  Shadow buffer: 128 cols × 48 rows × 2 bytes = 12 KB static storage.
 *  Supports up to 320 cols × 200 rows (64000 cells) safely.
 */

#include <stdarg.h>
#include "ark/types.h"
#include "ark/arch.h"
#include "ark/printk.h"
#include "psf_font_data.h"
#include "ark/log.h"

/* ── Serial COM1 ─────────────────────────────────────────────────────────── */
#define COM1 0x3F8
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline u8   inb (u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline bool tx_empty(void){return(inb(COM1+5)&0x20)!=0;}

static int serial_enabled = 0;

int serial_init(void){
    outb(COM1+3,0x80);
    outb(COM1+0,0x01);
    outb(COM1+1,0x00);
    outb(COM1+3,0x00);
    outb(COM1+4,0x00);
    outb(COM1+1,0x00);
    u8 lsr = inb(COM1+5);
    if ((lsr & 0x60) != 0x60) {
        return 0;
    }
    outb(COM1+1,0x00); outb(COM1+3,0x80); outb(COM1+0,0x03);
    outb(COM1+1,0x00); outb(COM1+3,0x03); outb(COM1+2,0xC7); outb(COM1+4,0x0B);
    serial_enabled = 1;
    return 1;
}
bool serial_has_input(void){return(inb(COM1+5)&0x01)!=0;}
u8   serial_getc(void){return serial_has_input()?inb(COM1):0;}
void serial_putc(char c){
    if (!serial_enabled) return;
    if(c=='\n'){while(!tx_empty());outb(COM1,'\r');}
    while(!tx_empty()); outb(COM1,(u8)c);
}

/* ── Jiffies / tick ──────────────────────────────────────────────────────── */
static u32 g_jiffies = 0;
static u32 g_hz      = 100;
u64  jiffies(void)              { return (u64)g_jiffies; }
void printk_set_hz(u32 hz)      { g_hz = hz ? hz : 100; }
u32  printk_tick(void)          { g_jiffies++; if((g_jiffies%5U)==0) printk_cursor_auto_update(); return g_jiffies; }

/* ── Framebuffer geometry ────────────────────────────────────────────────── */
#define FONT_W  8
#define FONT_H  PSF_CHARSIZE   /* 16 */

u8  *g_fb     = 0;
u32  g_pitch  = 0;
u32  g_bpp    = 32;   /* bits per pixel */
u32  g_Bpp    = 4;    /* bytes per pixel = g_bpp/8 */
u32  g_w      = 0;
u32  g_h      = 0;
static u32  g_col    = 0;
static u32  g_row    = 0;
static u32  g_fg     = 0x00AAAAAA;
static u32  g_bg     = 0x00000000;

/* cursor */
static bool g_cursor_en  = true;
static bool g_cursor_vis = true;
static u32  g_cursor_col = 0x00AAAAAA;
static u32  g_cursor_tk  = 0;

/* Set while fb_putc is actively writing characters.
 * The IRQ0 blink timer checks this and skips the redraw so it can never
 * race with text output and cause jiggle / torn lines. */
static volatile bool g_printing = false;

static inline u32 cols(void){ return g_w / FONT_W; }
static inline u32 rows(void){ return g_h / FONT_H; }

/* ── Shadow text buffer ──────────────────────────────────────────────────── */
/* Max 320×200 = 64000 cells. Each cell: lo byte = char, hi byte = packed
 * fg/bg index (we store raw 32-bit colors separately in the palette).
 * For simplicity we store char only; fg is always g_fg, bg always g_bg
 * (color changes are rare in printk output).
 * Memory: 64000 bytes ≈ 63 KB static — fine for a kernel. */
#define SHADOW_MAX_COLS  320
#define SHADOW_MAX_ROWS  200
#define SHADOW_CELLS     (SHADOW_MAX_COLS * SHADOW_MAX_ROWS)

static u8  g_shadow_ch[SHADOW_CELLS];   /* character at each cell      */
static u32 g_shadow_fg[SHADOW_CELLS];   /* foreground color            */
static bool g_shadow_valid = false;

static inline u32 cell(u32 c, u32 r){ return r * cols() + c; }

static void shadow_clear_row(u32 r){
    u32 nc = cols();
    for(u32 c = 0; c < nc && r*nc+c < SHADOW_CELLS; c++){
        g_shadow_ch[r*nc+c] = ' ';
        g_shadow_fg[r*nc+c] = g_fg;
    }
}

static void shadow_init(void){
    u32 nr = rows(), nc = cols();
    for(u32 r = 0; r < nr; r++)
        for(u32 c = 0; c < nc && r*nc+c < SHADOW_CELLS; c++){
            g_shadow_ch[r*nc+c] = ' ';
            g_shadow_fg[r*nc+c] = g_fg;
        }
    g_shadow_valid = true;
}

/* ── Pixel drawing (write-only, correct byte arithmetic) ─────────────────── */
static void draw_cell(u32 col, u32 row, unsigned char c, u32 fg){
    if(!g_fb || !g_pitch) return;
    if(col >= cols() || row >= rows()) return;
    if(c < 32 || c > 127) c = 32;
    const unsigned char *glyph = psf_glyphs[(u32)c];
    u32 px = col * FONT_W;
    u32 py = row * FONT_H;
    for(u32 gy = 0; gy < FONT_H; gy++){
        if(py + gy >= g_h) break;
        /* Use g_Bpp (bytes per pixel) so 16/24/32bpp all work correctly.
         * For 32bpp g_Bpp=4, for 24bpp g_Bpp=3, for 16bpp g_Bpp=2. */
        u8 *line = g_fb + (py + gy) * g_pitch + px * g_Bpp;
        u8 bits = glyph[gy];
        /* Write each pixel. For 32bpp cast to u32* for speed; for other
         * bpp write bytes individually to avoid alignment issues. */
        if(g_Bpp == 4){
            u32 *p = (u32*)line;
            p[0]=(bits&0x80)?fg:g_bg; p[1]=(bits&0x40)?fg:g_bg;
            p[2]=(bits&0x20)?fg:g_bg; p[3]=(bits&0x10)?fg:g_bg;
            p[4]=(bits&0x08)?fg:g_bg; p[5]=(bits&0x04)?fg:g_bg;
            p[6]=(bits&0x02)?fg:g_bg; p[7]=(bits&0x01)?fg:g_bg;
        } else {
            /* Generic: write g_Bpp bytes per pixel */
            for(u32 bit=0; bit<8; bit++){
                u32 color = (bits & (0x80u>>bit)) ? fg : g_bg;
                u8 *pp = line + bit * g_Bpp;
                for(u32 b=0; b<g_Bpp; b++) pp[b] = (u8)(color >> (b*8));
            }
        }
    }
}

static void draw_cursor_pixels(bool visible){
    if(!g_fb || !g_cursor_en) return;
    if(g_col >= cols() || g_row >= rows()) return;
    u32 px = g_col * FONT_W;
    u32 py = g_row * FONT_H;
    u32 ly = py + FONT_H - 2;
    if(ly + 1 >= g_h) return;
    u32 color = visible ? g_cursor_col : g_bg;
    if(g_Bpp == 4){
        u32 *l1 = (u32*)(g_fb + ly       * g_pitch + px * g_Bpp);
        u32 *l2 = (u32*)(g_fb + (ly + 1) * g_pitch + px * g_Bpp);
        for(u32 x = 0; x < FONT_W; x++) { l1[x] = color; l2[x] = color; }
    } else {
        u8 *l1 = g_fb + ly       * g_pitch + px * g_Bpp;
        u8 *l2 = g_fb + (ly + 1) * g_pitch + px * g_Bpp;
        for(u32 x = 0; x < FONT_W; x++){
            for(u32 b=0; b<g_Bpp; b++){
                l1[x*g_Bpp+b] = (u8)(color>>(b*8));
                l2[x*g_Bpp+b] = (u8)(color>>(b*8));
            }
        }
    }
}

/* Full screen redraw from shadow — used after scroll */
static void redraw_screen(void){
    if(!g_fb || !g_shadow_valid) return;
    u32 nr = rows(), nc = cols();
    for(u32 r = 0; r < nr; r++){
        for(u32 c = 0; c < nc; c++){
            u32 idx = r * nc + c;
            if(idx >= SHADOW_CELLS) break;
            draw_cell(c, r, g_shadow_ch[idx], g_shadow_fg[idx]);
        }
    }
}

/* ── Scroll — shadow-buffer based, no framebuffer reads ─────────────────── */
static void scroll(void){
    if(!g_fb || !g_pitch || !g_w || !g_h) return;
    u32 nr = rows(), nc = cols();
    if(!nr || !nc) return;

    /* Shift shadow up one row */
    u32 move = (u32)(nr - 1) * nc;
    if(move + nc > SHADOW_CELLS) move = SHADOW_CELLS - nc;
    for(u32 i = 0; i < move && i + nc < SHADOW_CELLS; i++){
        g_shadow_ch[i] = g_shadow_ch[i + nc];
        g_shadow_fg[i] = g_shadow_fg[i + nc];
    }
    /* Clear last row in shadow */
    shadow_clear_row(nr - 1);

    /* Redraw entire screen from clean shadow (no framebuffer reads) */
    redraw_screen();

    if(g_row > 0) g_row--;
}

/* ── Cursor API ──────────────────────────────────────────────────────────── */
void printk_cursor_enable(bool e){
    g_cursor_en = e;
    draw_cursor_pixels(e && g_cursor_vis);
}
void printk_cursor_set_color(u32 c){ g_cursor_col = c; draw_cursor_pixels(g_cursor_vis); }
void printk_cursor_toggle(void){
    if(!g_cursor_en) return;
    g_cursor_vis = !g_cursor_vis;
    g_cursor_tk  = 0;
    draw_cursor_pixels(g_cursor_vis);
}
void printk_cursor_move(int dx, int dy){
    if(!g_cursor_en) return;
    draw_cursor_pixels(false);
    int nc = (int)g_col + dx, nr = (int)g_row + dy;
    if(nc < 0) nc = 0; if(nc >= (int)cols()) nc = (int)cols()-1;
    if(nr < 0) nr = 0; if(nr >= (int)rows()) nr = (int)rows()-1;
    g_col = (u32)nc; g_row = (u32)nr;
    draw_cursor_pixels(g_cursor_vis);
}
void printk_cursor_auto_update(void){
    if(!g_cursor_en) return;
    /* Don't touch the framebuffer while fb_putc is mid-write — that's
     * what caused the jiggle / tearing on real hardware. */
    if(g_printing) return;
    if(++g_cursor_tk > 25){ g_cursor_tk = 0; printk_cursor_toggle(); }
}
void printk_cursor_update(void){ printk_cursor_auto_update(); }

/* ── printk_set_fb ────────────────────────────────────────────────────────
 * Called from arch entry after framebuffer address is determined.
 * pitch is in BYTES per scanline.
 * bpp: bits per pixel (16, 24, or 32).
 */
void printk_set_fb(u32 *addr, u32 w, u32 h, u32 pitch, u32 bpp){
    g_fb  = (u8*)addr;
    g_w   = w;
    g_h   = h;
    g_col = 0;
    g_row = 0;

    /* Store bpp and bytes-per-pixel */
    if(bpp == 16 || bpp == 24 || bpp == 32)
        g_bpp = bpp;
    else
        g_bpp = 32;   /* safe default */
    g_Bpp = g_bpp / 8;

    /* Pitch sanity: some GRUB versions report pitch in PIXELS, not bytes.
     * If pitch < width * g_Bpp it must be in pixels. */
    u32 min_pitch = w * g_Bpp;
    if(pitch > 0 && pitch < min_pitch)
        pitch = pitch * g_Bpp;    /* pixels → bytes */
    if(pitch < min_pitch || pitch == 0)
        pitch = min_pitch;
    g_pitch = pitch;

    shadow_init();
    draw_cursor_pixels(g_cursor_vis);
}

void printk_set_graphics_mode(bool e){ (void)e; }

/* ── fb_putc ─────────────────────────────────────────────────────────────── */
static void fb_putc(char c){
    if(!g_fb) return;

    /* Signal to IRQ0 blink timer that we're mid-print.
     * This prevents the timer from drawing cursor pixels while we are
     * writing characters, which was the cause of the jiggle on real HW. */
    g_printing = true;

    /* Erase cursor from its current position before moving/writing.
     * We only do this ONCE at the start of fb_putc, not per-character —
     * the old code did erase+redraw on every character which caused the
     * visible flicker during fast text output (PCI scan etc.). */
    draw_cursor_pixels(false);

    if(c == '\n'){ g_col = 0; g_row++; }
    else if(c == '\r'){ g_col = 0; }
    else if(c == '\b'){
        if(g_col > 0){
            g_col--;
            u32 idx = cell(g_col, g_row);
            if(idx < SHADOW_CELLS){ g_shadow_ch[idx]=' '; g_shadow_fg[idx]=g_fg; }
            draw_cell(g_col, g_row, ' ', g_fg);
        }
    } else {
        u32 idx = cell(g_col, g_row);
        if(idx < SHADOW_CELLS){ g_shadow_ch[idx]=(u8)c; g_shadow_fg[idx]=g_fg; }
        draw_cell(g_col, g_row, (unsigned char)c, g_fg);
        g_col++;
    }

    if(g_col >= cols()){ g_col = 0; g_row++; }
    if(g_row >= rows()) scroll();

    /* Cursor is NOT redrawn here. The blink timer (fb_cursor_tick, called
     * from IRQ0 every 25 ticks = 250ms) is the sole place that draws the
     * cursor. This means the cursor never flickers during text bursts. */
    g_printing = false;
}

/* ── VGA text fallback (when no framebuffer) ─────────────────────────────── */
#define VGA_W 80
#define VGA_H 25
static volatile u16 *const vga_buf = (volatile u16*)0xB8000;
static u32 vga_row = 0, vga_col = 0;
static void vga_scroll(void){
    for(u32 r=1;r<VGA_H;r++)
        for(u32 c=0;c<VGA_W;c++)
            vga_buf[(r-1)*VGA_W+c]=vga_buf[r*VGA_W+c];
    for(u32 c=0;c<VGA_W;c++) vga_buf[(VGA_H-1)*VGA_W+c]=0x0720;
    if(vga_row>0) vga_row--;
}
static void vga_putc(char c){
    if(c=='\n'){vga_col=0;vga_row++;}
    else if(c=='\r'){vga_col=0;}
    else{vga_buf[vga_row*VGA_W+vga_col]=0x0700|(u8)c;vga_col++;}
    if(vga_col>=VGA_W){vga_col=0;vga_row++;}
    if(vga_row>=VGA_H) vga_scroll();
}

/* ── Low-level output ────────────────────────────────────────────────────── */
/* Forward-declare display_putc so we don't need to pull in the full header
 * (printk.c is a low-level file with minimal includes). */
extern void display_putc(char c) __attribute__((weak));

static void putc_out(char c){
    /* Always use display_putc when available - it's the primary display driver.
     * The fb_putc path is disabled to avoid double rendering. */
    if(display_putc) {
        display_putc(c);
    } else if(g_fb) {
        fb_putc(c);
    } else {
        vga_putc(c);
    }
    serial_putc(c);
    log_putchar(c);
    
    extern void panic_log_putchar(char c) __attribute__((weak));
    panic_log_putchar(c);
}

/* ── Number formatting ───────────────────────────────────────────────────── */
static void put_uint32(u32 v, u32 base, bool upper){
    const char *d=upper?"0123456789ABCDEF":"0123456789abcdef";
    char buf[32]; int i=0;
    if(!v){putc_out('0');return;}
    while(v){buf[i++]=d[v%base];v/=base;}
    for(int j=i-1;j>=0;j--) putc_out(buf[j]);
}
/* 64-bit print using only 32-bit division — avoids __udivdi3/__umoddi3.
 * Algorithm: long-divide the 64-bit value by base using hi/lo word split.
 *   q_hi = hi / base,  r_hi = hi % base
 *   combined = r_hi * 2^32 + lo  (at most 15 * 2^32 + 2^32-1 < 2^36)
 *   But combined/base still needs 64-bit division.
 * Instead, we handle the two common bases separately:
 *   hex (base=16): just shift — no division needed.
 *   decimal (base=10): use the standard double-dabble / subtract approach.
 * For any other base we fall back to the shift+subtract loop.
 */
static u32 u64_divmod32(u64 *pv, u32 base) {
    /* Performs *pv = *pv / base, returns *pv % base.
     * Uses only 32-bit division by processing hi word first. */
    u32 hi  = (u32)(*pv >> 32);
    u32 lo  = (u32)(*pv);
    u32 qhi = hi / base;
    u32 rhi = hi - qhi * base;          /* == hi % base, no __umoddi3 */
    /* Now compute (rhi<<32 | lo) / base using extended division:
     * split lo into two 16-bit halves and do two steps of long division. */
    u32 tmp, qlo, rem;
    /* Step 1: divide (rhi : lo_high16) by base */
    tmp = (rhi << 16) | (lo >> 16);
    u32 q1 = tmp / base;
    u32 r1 = tmp - q1 * base;
    /* Step 2: divide (r1 : lo_low16) by base */
    tmp = (r1 << 16) | (lo & 0xFFFFu);
    u32 q2  = tmp / base;
    rem     = tmp - q2 * base;
    qlo     = (q1 << 16) | q2;
    *pv = ((u64)qhi << 32) | qlo;
    return rem;
}
static void put_uint64(u64 v, u32 base, bool upper){
    const char *d=upper?"0123456789ABCDEF":"0123456789abcdef";
    char buf[64]; int i=0;
    if(!v){putc_out('0');return;}
    while(v){ buf[i++]=d[u64_divmod32(&v,base)]; }
    for(int j=i-1;j>=0;j--) putc_out(buf[j]);
}
static void put_int(int v){
    if(v<0){putc_out('-');put_uint32((u32)(-v),10,false);}
    else put_uint32((u32)v,10,false);
}
static void put_str(const char *s){if(!s)s="(null)";while(*s)putc_out(*s++);}

/* ── Enhanced Address Printing ───────────────────────────────────────────── */
static void put_ptr_width(u64 v, int width){
    const char *d = "0123456789abcdef";
    char buf[32];
    int i = 0;
    if(!v){
        putc_out('0');return;
    }
    while(v && i < 32){
        buf[i++] = d[v & 0xF];
        v >>= 4;
    }
    while(i < width) buf[i++] = '0';
    for(int j = i-1; j >= 0; j--) putc_out(buf[j]);
}

void printk_put_ptr(void *ptr){
    putc_out('0');putc_out('x');
    put_ptr_width((u64)(usize)ptr, sizeof(void*) * 2);
}

void printk_put_phys(u64 phys){
    putc_out('0');putc_out('x');
    put_ptr_width(phys, sizeof(phys_addr_t) * 2);
}

void printk_put_hex(u8 byte){
    const char *d = "0123456789abcdef";
    putc_out(d[(byte >> 4) & 0xF]);
    putc_out(d[byte & 0xF]);
}

void printk_hex_dump(const char *name, const void *addr, u32 len){
    const u8 *p = (const u8 *)addr;
    printk("[HEX] %s: %p +%u bytes\n", name ? name : "dump", addr, len);
    for(u32 i = 0; i < len; i++){
        if(i % 16 == 0){
            if(i > 0) putc_out('\n');
            printk_put_ptr((void*)(usize)(p + i));
            putc_out(':');
            putc_out(' ');
        }else if(i % 8 == 0){
            putc_out(' ');
        }
        printk_put_hex(p[i]);
        putc_out(' ');
    }
    if(len > 0 && len % 16 != 0) putc_out('\n');
}

/* ── Timestamp ───────────────────────────────────────────────────────────── */
const char *_PRINTK_T_SENTINEL = (const char*)0x1;
/* ── TSC calibration against PIT channel 2 ──────────────────────────────
 * PIT ch2 is gated by port 0x61 bit 0 and its output is readable on
 * bit 5. We program it for a known interval, count TSC ticks, and get
 * the real CPU frequency without any hardcoded constant.
 *
 * Gate sequence:
 *   1. Set port 0x61 bit 0 (gate enable), clear bit 1 (speaker off)
 *   2. Write command 0xB0: ch2, lo/hi, one-shot mode 0
 *   3. Load divisor → duration = divisor / 1193182 seconds
 *   4. Wait for OUT to go low (bit 5 of 0x61 clears)
 *   5. TSC delta / duration = TSC Hz
 *
 * Divisor 11932 → ~10ms window. Safe on all real hardware. */
static u32 tsc_hz_per_us = 1000;   /* TSC ticks per microsecond — calibrated */
static u32 tsc_mhz       = 1000;   /* CPU MHz — derived from calibration      */
static u32 ts_last=0, ts_accum=0;

static inline u32 rdtsc32(void){
    u32 lo; __asm__ volatile("rdtsc":"=a"(lo)::"edx"); return lo;
}

void tsc_calibrate(void){
    /* 11932 ticks ≈ 10ms at 1.193182MHz */
    const u16 div = 11932;
    /* Save and set gate: bit0=gate, bit1=speaker (keep off) */
    u8 ctrl = inb(0x61);
    outb(0x61, (ctrl & ~0x02u) | 0x01u);
    /* Program ch2: command 0xB0 = ch2 | lo/hi | mode0 | binary */
    outb(0x43, 0xB0);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)(div >> 8));
    /* Wait for OUT to go high (bit 5 set = countdown complete) */
    u32 t0 = rdtsc32();
    while(!(inb(0x61) & 0x20));
    u32 delta = rdtsc32() - t0;
    /* Restore gate */
    outb(0x61, ctrl & ~0x01u);
    /* delta TSC ticks elapsed during div/1193182 seconds
     * TSC Hz = delta * 1193182 / div
     * MHz    = TSC Hz / 1000000
     * ticks_per_us = TSC Hz / 1000000  (= MHz) */
    /* Use 32-bit safe arithmetic: delta * 1193 / div (loses some precision but avoids u64) */
    u32 mhz = (delta / div) * 1193u / 1000u;
    if(!mhz) mhz = 1000;   /* fallback 1GHz if calibration failed */
    tsc_mhz       = mhz;
    tsc_hz_per_us = mhz;
}

/* Returns measured CPU frequency in MHz */
u32 tsc_get_mhz(void){ return tsc_mhz; }
void tsc_set_hz(u32 t){ tsc_hz_per_us = t ? t : 1000; }
static void print_ts(void){
    u32 lo=rdtsc32();
    ts_accum+=(lo-ts_last)/tsc_hz_per_us;
    ts_last=lo;
    u32 s=ts_accum/1000000, us=ts_accum%1000000;
    putc_out('[');
    if(s<1000)putc_out(' ');if(s<100)putc_out(' ');
    if(s<10) putc_out(' ');
    put_uint32(s,10,false); putc_out('.');
    if(us<100000)putc_out('0');if(us<10000)putc_out('0');
    if(us<1000) putc_out('0');if(us<100)  putc_out('0');
    if(us<10)   putc_out('0');
    put_uint32(us,10,false); putc_out(']'); putc_out(' ');
}

/* ── Public printk API ───────────────────────────────────────────────────── */
#if CONFIG_PRINTK_ENABLE

int vprintk(const char *fmt, va_list ap){
    int n=0;
    while(*fmt){
        if(*fmt!='%'){putc_out(*fmt++);n++;continue;}
        fmt++;
        int is_ll=0, is_l=0;
        if(*fmt=='l'){fmt++;if(*fmt=='l'){fmt++;is_ll=1;}else is_l=1;}
        switch(*fmt++){
        case '%': putc_out('%');n++;break;
        case 'c': putc_out((char)va_arg(ap,int));n++;break;
        case 's': put_str(va_arg(ap,char*));break;
        case 'd': case 'i':
            if(is_ll){ long long v=va_arg(ap,long long);
                if(v<0){putc_out('-');put_uint64((u64)(-v),10,false);}
                else put_uint64((u64)v,10,false);
            }else if(is_l){ long v=va_arg(ap,long);
                if(v<0){putc_out('-');put_uint32((u32)(-(unsigned long)v),10,false);}
                else put_uint32((u32)v,10,false);
            }else put_int(va_arg(ap,int));
            break;
        case 'u':
            if(is_ll) put_uint64(va_arg(ap,unsigned long long),10,false);
            else if(is_l) put_uint32((u32)va_arg(ap,unsigned long),10,false);
            else put_uint32(va_arg(ap,u32),10,false);
            break;
        case 'x':
            if(is_ll) put_uint64(va_arg(ap,unsigned long long),16,false);
            else if(is_l) put_uint32((u32)va_arg(ap,unsigned long),16,false);
            else put_uint32(va_arg(ap,u32),16,false);
            break;
        case 'X':
            if(is_ll) put_uint64(va_arg(ap,unsigned long long),16,true);
            else if(is_l) put_uint32((u32)va_arg(ap,unsigned long),16,true);
            else put_uint32(va_arg(ap,u32),16,true);
            break;
        case 'p':{
            unsigned long v=(unsigned long)va_arg(ap,void*);
            putc_out('0');putc_out('x');
            put_ptr_width(v, sizeof(unsigned long) * 2);
            break;}
        case 'P':{
            u64 v=(u64)(usize)va_arg(ap,void*);
            putc_out('0');putc_out('x');
            put_ptr_width(v, sizeof(void*) * 2);
            break;}
        case 'h':{
            u64 v=(u64)(usize)va_arg(ap,void*);
            putc_out('0');putc_out('x');
            put_ptr_width(v, sizeof(phys_addr_t) * 2);
            break;}
        default: break;
        }
    }
    return n;
}

int printk(const char *fmt,...){
    va_list ap; va_start(ap,fmt); int n;
    if(fmt==_PRINTK_T_SENTINEL){
        const char *f=va_arg(ap,const char*);
        print_ts(); n=vprintk(f,ap);
    }else{ n=vprintk(fmt,ap); }
    va_end(ap); return n;
}

int printc(u8 color, const char *fmt,...){
    static const u32 pal[16]={
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF};
    u32 old=g_fg; g_fg=pal[color&0xF];
    va_list ap; va_start(ap,fmt); int n=vprintk(fmt,ap); va_end(ap);
    g_fg=old; return n;
}

int printc_rgb(u32 fg, const char *fmt,...){
    u32 old=g_fg; g_fg=fg;
    va_list ap; va_start(ap,fmt); int n=vprintk(fmt,ap); va_end(ap);
    g_fg=old; return n;
}

int printk_info(const char *fmt,...){
    va_list ap; va_start(ap,fmt); int n=vprintk(fmt,ap); va_end(ap); return n;
}

#else
int vprintk(const char *f,va_list a){(void)f;(void)a;return 0;}
int printk(const char *f,...){(void)f;return 0;}
int printc(u8 c,const char *f,...){(void)c;(void)f;return 0;}
int printc_rgb(u32 c,const char *f,...){(void)c;(void)f;return 0;}
int printk_info(const char *f,...){(void)f;return 0;}
#endif
