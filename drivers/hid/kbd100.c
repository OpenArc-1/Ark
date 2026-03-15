/**
 * drivers/hid/kbd100.c — Robust PS/2 Keyboard Driver
 *
 * Works on: QEMU, VirtualBox, VMware, Hyper-V, bare metal.
 *
 * - Full PS/2 controller init (CCB, disable IRQs+translation, scancode set 1)
 * - Timeout-guarded reads — never hangs
 * - E0 extended and E1 (Pause) scancode handling
 * - Polled; IRQ1 pending flag still cleared by input_poll
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/kbd.h"

/* ── PS/2 constants ─────────────────────────────────────────────── */
#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define PS2_OBF       0x01
#define PS2_IBF       0x02
#define PS2_SYS       0x04
#define PS2_TIMEO     0x40
#define PS2_PARITY    0x80

#define CTL_DISABLE_P1 0xAD
#define CTL_ENABLE_P1  0xAE
#define CTL_DISABLE_P2 0xA7
#define CTL_READ_CCB   0x20
#define CTL_WRITE_CCB  0x60
#define CTL_SELFTEST   0xAA
#define CTL_P1_TEST    0xAB

#define CCB_INT1 0x01
#define CCB_INT2 0x02
#define CCB_SYS  0x04
#define CCB_XLAT 0x40

#define KBD_SCANCODE 0xF0
#define KBD_ENABLE   0xF4
#define KBD_RESET    0xFF
#define KBD_ACK      0xFA
#define KBD_RESEND   0xFE

#define LONG_T  500000
#define SHORT_T 100000

/* ── I/O helpers ────────────────────────────────────────────────── */
static inline u8   _inb(u16 p){ u8 v; asm volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void _outb(u16 p,u8 v){ asm volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void _iow(void){ asm volatile("outb %%al,$0x80"::"a"((u8)0)); }

static void _ww(void){ int t=LONG_T; while((_inb(PS2_STATUS)&PS2_IBF)&&--t)_iow(); }
static int  _wr(int t){ while(!(_inb(PS2_STATUS)&PS2_OBF)&&--t)_iow(); return t; }
static int  _rd(int t){ return _wr(t)?(int)_inb(PS2_DATA):-1; }
static void _cc(u8 c){ _ww(); _outb(PS2_CMD, c); }
static void _cd(u8 d){ _ww(); _outb(PS2_DATA,d); }
static void _drain(void){
    int i=0;
    while(i++<32&&(_inb(PS2_STATUS)&PS2_OBF)){_iow();_inb(PS2_DATA);_iow();}
}
static int _kc(u8 c){
    int i,r;
    for(i=0;i<3;i++){
        _ww(); _outb(PS2_DATA,c);
        r=_rd(LONG_T);
        if(r==KBD_ACK)   return 1;
        if(r!=KBD_RESEND) break;
    }
    return 0;
}

/* ── State ──────────────────────────────────────────────────────── */
static bool shift=false, caps=false, ctrl=false, alt=false, ext=false;
/* kbd_irq_pending is defined in arch/x86/int80.S — extern declared in kbd.h */

/* ── Ring buffer ────────────────────────────────────────────────── */
#define BUF 256
static char kbuf[BUF];
static int khead=0, ktail=0;
void kput(int c){ int n=(khead+1)%BUF; if(n!=ktail){kbuf[khead]=c;khead=n;} }

/* ── Scancode tables (set 1, US QWERTY) ───────────────────────── */
static const char _nm[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
  'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
  'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
  'b','n','m',',','.','/', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0, '7','8','9','-','4','5','6','+','1',
  '2','3','0','.', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};
static const char _sh[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
  'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
  'D','F','G','H','J','K','L',':','"', '~', 0, '|', 'Z','X','C','V',
  'B','N','M','<','>','?', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0, '7','8','9','-','4','5','6','+','1',
  '2','3','0','.', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

/* ── kbd_init ───────────────────────────────────────────────────── */
void kbd_init(void) {
    u8 ccb;

    _cc(CTL_DISABLE_P1); _cc(CTL_DISABLE_P2);
    _drain();

    _cc(CTL_READ_CCB);
    ccb = (u8)_rd(LONG_T);
    ccb &= ~(u8)(CCB_INT1|CCB_INT2|CCB_XLAT);
    ccb |=  (u8) CCB_SYS;
    _cc(CTL_WRITE_CCB); _cd(ccb);

    _cc(CTL_SELFTEST); _rd(LONG_T);
    _cc(CTL_P1_TEST);  _rd(LONG_T);

    _cc(CTL_ENABLE_P1);
    _drain();

    _kc(KBD_RESET);    _rd(LONG_T); _drain();
    _kc(KBD_ENABLE);
    _kc(KBD_SCANCODE); _kc(0x01);
    _drain();

    shift=caps=ctrl=alt=ext=false;
    printk(T,"kbd: PS/2 ready\n");
}

/* ── kbd_poll ───────────────────────────────────────────────────── */
int kbd_poll(void) {
    int i;
    for(i=0;i<64;i++){
        u8 st=_inb(PS2_STATUS);
        if(!(st&PS2_OBF)) break;
        if(st&(PS2_TIMEO|PS2_PARITY)){ _inb(PS2_DATA); continue; }

        u8 sc=_inb(PS2_DATA);

        if(sc==0xE0){ ext=true; continue; }
        if(sc==0xE1){ _rd(SHORT_T); _rd(SHORT_T); ext=false; continue; }
        if(sc==KBD_ACK||sc==KBD_RESEND){ ext=false; continue; }

        bool e=ext; ext=false;
        bool rel=(sc&0x80)!=0;
        u8 mk=sc&0x7F;

        if(e){
            if(!rel) switch(mk){
                case 0x48: kput(KEY_ARROW_UP);    break;
                case 0x50: kput(KEY_ARROW_DOWN);  break;
                case 0x4B: kput(KEY_ARROW_LEFT);  break;
                case 0x4D: kput(KEY_ARROW_RIGHT); break;
                case 0x47: kput(KEY_HOME);         break;
                case 0x4F: kput(KEY_END);          break;
                case 0x49: kput(KEY_PGUP);         break;
                case 0x51: kput(KEY_PGDN);         break;
                case 0x52: kput(KEY_INS);          break;
                case 0x53: kput(KEY_DEL);          break;
                case 0x1C: kput('\n');              break;
                case 0x35: kput('/');              break;
            }
            continue;
        }

        /* modifiers */
        if(mk==42||mk==54){ shift=!rel; continue; }
        if(mk==29)         { ctrl =!rel; continue; }
        if(mk==56)         { alt  =!rel; continue; }
        if(mk==58&&!rel)   { caps =!caps; continue; }

        if(!rel&&mk<128){
            char n=_nm[mk], s=_sh[mk];
            char c=(n>='a'&&n<='z')?((shift^caps)?s:n):(shift?s:n);
            if(c) kput(c);
        }
    }
    kbd_irq_pending=0;
}

/* ── Public API ─────────────────────────────────────────────────── */
char kbd_getc(void){
    if(khead==ktail) return 0;
    char c=kbuf[ktail]; ktail=(ktail+1)%BUF; return c;
}
bool kbd_has_input(void)     { return khead!=ktail; }
bool kbd_is_initialized(void){ return true; }
void kbd_get_key_state(bool*s,bool*c,bool*a){
    if(s)*s=shift; if(c)*c=ctrl; if(a)*a=alt;
}
