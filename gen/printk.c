/**
 * printk.c - kernel text output via PSF font on VESA framebuffer + COM1 serial
 */

#include <stdarg.h>
#include "ark/types.h"
#include "ark/printk.h"
#include "psf_font_data.h"

/* ── Serial COM1 ─────────────────────────────────────────── */
#define COM1 0x3F8
static inline void outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline u8   inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline bool tx_empty(void){return(inb(COM1+5)&0x20)!=0;}

void serial_init(void){
    outb(COM1+1,0x00);outb(COM1+3,0x80);outb(COM1+0,0x03);
    outb(COM1+1,0x00);outb(COM1+3,0x03);outb(COM1+2,0xC7);outb(COM1+4,0x0B);
}
bool serial_has_input(void){return(inb(COM1+5)&0x01)!=0;}
u8   serial_getc(void){return serial_has_input()?inb(COM1):0;}
void serial_putc(char c){
    if(c=='\n'){while(!tx_empty());outb(COM1,'\r');}
    while(!tx_empty());outb(COM1,(u8)c);
}

/* ── Framebuffer ─────────────────────────────────────────── */
#define FONT_W 8
#define FONT_H PSF_CHARSIZE

static u8  *g_fb    = 0;
static u32  g_pitch = 0;
static u32  g_w     = 0;
static u32  g_h     = 0;
static u32  g_col   = 0;
static u32  g_row   = 0;
static u32  g_fg    = 0x00FFFFFF;
static u32  g_bg    = 0x00000000;
static bool g_cursor_enabled = true;
static u32  g_cursor_color = 0x00FFFFFF;  /* Magenta block cursor by default */
static u32  g_cursor_ticker = 0;
static bool g_cursor_visible = true;

/* Forward declarations */
static u32 cols(void);
static u32 rows(void);
static void draw_glyph(u32 col, u32 row, unsigned char c);
static void draw_cursor(void);
static void erase_cursor(void);

void printk_set_graphics_mode(bool e){(void)e;}

static u32 cols(void){return g_w/FONT_W;}
static u32 rows(void){return g_h/FONT_H;}

/* Forward implementations */
static void draw_glyph(u32 col, u32 row, unsigned char c);

static void draw_cursor(void){
    if(!g_fb || !g_cursor_enabled || !g_cursor_visible) return;
    if(g_col >= cols() || g_row >= rows()) return;
    
    u32 px = g_col * FONT_W;
    u32 py = g_row * FONT_H;
    
    /* Draw full block cursor */
    for(u32 cy = 0; cy < FONT_H; cy++){
        u32 *line = (u32*)(g_fb + (py + cy) * g_pitch);
        for(u32 cx = 0; cx < FONT_W; cx++){
            line[px + cx] = g_cursor_color;
        }
    }
}

static void erase_cursor(void){
    if(!g_fb || !g_cursor_enabled) return;
    if(g_col >= cols() || g_row >= rows()) return;
    
    u32 px = g_col * FONT_W;
    u32 py = g_row * FONT_H;
    
    /* Restore background */
    for(u32 cy = 0; cy < FONT_H; cy++){
        u32 *line = (u32*)(g_fb + (py + cy) * g_pitch);
        for(u32 cx = 0; cx < FONT_W; cx++){
            line[px + cx] = g_bg;
        }
    }
}

void printk_cursor_enable(bool enable){
    g_cursor_enabled = enable;
    if(!enable){
        erase_cursor();
    }else{
        draw_cursor();
    }
}

void printk_cursor_set_color(u32 color){
    erase_cursor();
    g_cursor_color = color;
    draw_cursor();
}

void printk_cursor_toggle(void){
    if(!g_cursor_enabled) return;
    g_cursor_visible = !g_cursor_visible;
    g_cursor_ticker = 0;
    if(g_cursor_visible){
        draw_cursor();
    }else{
        erase_cursor();
    }
}

void printk_cursor_move(int dx, int dy){
    if(!g_cursor_enabled) return;
    erase_cursor();
    
    int new_col = (int)g_col + dx;
    int new_row = (int)g_row + dy;
    
    if(new_col < 0) new_col = 0;
    if(new_col >= (int)cols()) new_col = cols() - 1;
    if(new_row < 0) new_row = 0;
    if(new_row >= (int)rows()) new_row = rows() - 1;
    
    g_col = new_col;
    g_row = new_row;
    
    draw_cursor();
}

/* Auto-update mechanism (called from timer interrupt or main loop) */
void printk_cursor_auto_update(void){
    if(!g_cursor_enabled) return;
    g_cursor_ticker++;
    if(g_cursor_ticker > 25){  /* Blink every ~25 ticks */
        g_cursor_ticker = 0;
        printk_cursor_toggle();
    }
}

/* Deprecated: use printk_cursor_auto_update instead */
void printk_cursor_update(void){
    printk_cursor_auto_update();
}

void printk_set_fb(u32 *addr, u32 w, u32 h, u32 pitch){
    g_fb=((u8*)addr); g_w=w; g_h=h; g_pitch=pitch; g_col=0; g_row=0;
    draw_cursor();
}

static void scroll(void){
    /* Move the entire visible area up by one character row.
     * Copy FONT_H pixel rows at a time using word-width memmove semantics.
     * pitch is in bytes; g_w pixels * 4 bytes = g_w*4 bytes per row.
     * Using __builtin_memcpy on 4-byte aligned rows lets the compiler
     * emit rep movsd / SSE moves instead of a scalar loop — ~10x faster. */
    u32 row_bytes = g_w * 4;
    u32 char_bytes = FONT_H * g_pitch;
    u8 *dst = g_fb;
    u8 *src = g_fb + char_bytes;
    u8 *end = g_fb + g_h * g_pitch - char_bytes;
    /* copy all rows except the last FONT_H upward */
    while(dst < end){
        __builtin_memcpy(dst, src, row_bytes);
        dst += g_pitch;
        src += g_pitch;
    }
    /* clear the bottom FONT_H rows */
    u32 *p = (u32*)(g_fb + (g_h - FONT_H) * g_pitch);
    u32 total = (FONT_H * g_pitch) / 4;
    for(u32 i = 0; i < total; i++) p[i] = g_bg;
    if(g_row > 0) g_row--;
}

static void draw_glyph(u32 col, u32 row, unsigned char c){
    if(!g_fb||!g_pitch)return;
    if(c<32||c>127)c=32;
    const unsigned char *glyph=psf_glyphs[(u32)c];
    u32 px=col*FONT_W, py=row*FONT_H;
    for(u32 gy=0;gy<FONT_H;gy++){
        u32 *line=(u32*)(g_fb+(py+gy)*g_pitch);
        u8 bits=glyph[gy];
        for(u32 gx=0;gx<FONT_W;gx++)
            line[px+gx]=(bits>>(7-gx))&1?g_fg:g_bg;
    }
}

static void fb_putc(char c){
    if(!g_fb)return;
    erase_cursor();
    if(c=='\n'){g_col=0;g_row++;}
    else if(c=='\r'){g_col=0;}
    else if(c=='\b'){if(g_col>0){g_col--;draw_glyph(g_col,g_row,' ');}}
    else{draw_glyph(g_col,g_row,(unsigned char)c);g_col++;}
    if(g_col>=cols()){g_col=0;g_row++;}
    if(g_row>=rows())scroll();
    draw_cursor();
}

/* ── Low-level output ────────────────────────────────────── */
/* ── VGA text fallback (used when g_fb == 0) ────────────────── */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
static volatile u16 *const vga_buf = (volatile u16 *)0xB8000;
static u32 vga_row = 0, vga_col = 0;
static void vga_scroll(void){
    for(u32 r=1;r<VGA_HEIGHT;r++)
        for(u32 c=0;c<VGA_WIDTH;c++)
            vga_buf[(r-1)*VGA_WIDTH+c]=vga_buf[r*VGA_WIDTH+c];
    for(u32 c=0;c<VGA_WIDTH;c++)
        vga_buf[(VGA_HEIGHT-1)*VGA_WIDTH+c]=0x0F20;
    if(vga_row>0)vga_row--;
}
static void vga_putc(char c){
    if(c=='\n'){vga_col=0;vga_row++;}
    else if(c=='\r'){vga_col=0;}
    else{vga_buf[vga_row*VGA_WIDTH+vga_col]=0x0F00|(u8)c;vga_col++;}
    if(vga_col>=VGA_WIDTH){vga_col=0;vga_row++;}
    if(vga_row>=VGA_HEIGHT)vga_scroll();
}

static void putc_out(char c){
    if(g_fb) fb_putc(c);
    else     vga_putc(c);
    serial_putc(c);
}

/* ── Number/string formatting (defined BEFORE anything that calls them) ── */
static void put_uint(u32 v, u32 base, bool upper){
    const char *d=upper?"0123456789ABCDEF":"0123456789abcdef";
    char buf[32]; int i=0;
    if(!v){putc_out('0');return;}
    while(v){buf[i++]=d[v%base];v/=base;}
    for(int j=i-1;j>=0;j--)putc_out(buf[j]);
}

static void put_int(int v){
    if(v<0){putc_out('-');put_uint((u32)(-v),10,false);}
    else put_uint((u32)v,10,false);
}

static void put_str(const char *s){if(!s)s="(null)";while(*s)putc_out(*s++);}

/* ── Timestamp ───────────────────────────────────────────── */
const char *_PRINTK_T_SENTINEL=(const char*)0x1;

static u32 tsc_hz_per_us=1000;
static u32 ts_last=0, ts_accum=0;

static u32 rdtsc32(void){
    u32 lo;__asm__ volatile("rdtsc":"=a"(lo)::"edx");return lo;
}

void tsc_calibrate(void){
    u16 div=59659;
    u8 ctrl=inb(0x61);ctrl&=~0x02;ctrl|=0x01;outb(0x61,ctrl);
    outb(0x43,0xB0);outb(0x42,(u8)div);outb(0x42,(u8)(div>>8));
    u32 s=rdtsc32();while(inb(0x61)&0x20);
    u32 delta=rdtsc32()-s;
    tsc_hz_per_us=(delta*20)/1000000;
    if(!tsc_hz_per_us)tsc_hz_per_us=1;
    outb(0x61,ctrl&~0x01);
}
void tsc_set_hz(u32 t){tsc_hz_per_us=t;}

static void print_ts(void){
    u32 lo=rdtsc32();
    ts_accum+=(lo-ts_last)/tsc_hz_per_us;
    ts_last=lo;
    u32 s=ts_accum/1000000, us=ts_accum%1000000;
    putc_out('[');
    if(s<1000)putc_out(' ');
    if(s< 100)putc_out(' ');
    if(s<  10)putc_out(' ');
    put_uint(s,10,false);
    putc_out('.');
    if(us<100000)putc_out('0');
    if(us< 10000)putc_out('0');
    if(us<  1000)putc_out('0');
    if(us<   100)putc_out('0');
    if(us<    10)putc_out('0');
    put_uint(us,10,false);
    putc_out(']');putc_out(' ');
}

/* ── Public API ──────────────────────────────────────────── */
#if CONFIG_PRINTK_ENABLE

int vprintk(const char *fmt, va_list ap){
    int n=0;
    while(*fmt){
        if(*fmt!='%'){putc_out(*fmt++);n++;continue;}
        fmt++;
        switch(*fmt++){
            case '%':putc_out('%');n++;break;
            case 'c':putc_out((char)va_arg(ap,int));n++;break;
            case 's':put_str(va_arg(ap,char*));break;
            case 'd':case 'i':put_int(va_arg(ap,int));break;
            case 'u':put_uint(va_arg(ap,u32),10,false);break;
            case 'x':put_uint(va_arg(ap,u32),16,false);break;
            case 'X':put_uint(va_arg(ap,u32),16,true);break;
            default:break;
        }
    }
    return n;
}

int printk(const char *fmt,...){
    va_list ap;va_start(ap,fmt);int n;
    if(fmt==_PRINTK_T_SENTINEL){
        const char *f=va_arg(ap,const char*);
        print_ts();n=vprintk(f,ap);
    }else{n=vprintk(fmt,ap);}
    va_end(ap);return n;
}

int printc(u8 color, const char *fmt,...){
    static const u32 pal[16]={
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF
    };
    u32 old=g_fg;g_fg=pal[color&0xF];
    va_list ap;va_start(ap,fmt);int n=vprintk(fmt,ap);va_end(ap);
    g_fg=old;return n;
}

int printc_rgb(u32 fg, const char *fmt,...){
    u32 old=g_fg;g_fg=fg;
    va_list ap;va_start(ap,fmt);int n=vprintk(fmt,ap);va_end(ap);
    g_fg=old;return n;
}

#else
int vprintk(const char *f,va_list a){(void)f;(void)a;return 0;}
int printk(const char *f,...){(void)f;return 0;}
int printc(u8 c,const char *f,...){(void)c;(void)f;return 0;}
int printc_rgb(u32 c,const char *f,...){(void)c;(void)f;return 0;}
#endif
