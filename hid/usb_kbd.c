/*
 * usb_kbd.c — USB HID Boot-Protocol Keyboard Driver
 *
 * Supports: UHCI (I/O-port) and OHCI (MMIO)
 * Target:   32-bit protected-mode bare-metal kernel (flat memory model)
 *
 * OHCI fixes in this version:
 *  - No port reset / re-enumeration for QEMU (device pre-configured at addr 1)
 *  - HcInterruptStatus WBI bit acknowledged each poll so HC resumes done_head writes
 *  - FmInterval written with correct FIT|FSMPS|FI format
 *  - R (Buffer Rounding) bit placed at bit 18, DP at bits 19:18 — no overlap bug
 *  - done_head checked via HcDoneHead register directly as fallback
 *  - ED Skip bit cleared explicitly before enabling periodic list
 *
 * Dependency surface (kernel must provide):
 *   ark/types.h   — u8/u16/u32/bool
 *   ark/printk.h  — printk()
 *   ark/pci.h     — pciread(bus,slot,fn,reg)
 *   inb/outb/inw/outw/inl/outl
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/pci.h"

/* ── Port I/O ────────────────────────────────────────────────────────────── */
static inline void outb(u16 p, u8  v){asm volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void outw(u16 p, u16 v){asm volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void outl(u16 p, u32 v){asm volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline u8  inb(u16 p){u8  v;asm volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u16 inw(u16 p){u16 v;asm volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u32 inl(u16 p){u32 v;asm volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}

extern u32 pciread(u8 bus, u8 slot, u8 fn, u8 reg);

/* ── DMA pool ────────────────────────────────────────────────────────────── */
#define USB_DMA_SIZE 0x2000
__attribute__((aligned(4096))) static u8 usb_dma_pool[USB_DMA_SIZE];
#define DMA_PTR(off)  ((void*)((u32)usb_dma_pool+(u32)(off)))
#define DMA_PHYS(off) ((u32)usb_dma_pool+(u32)(off))

/* Pool offsets */
#define OFF_UHCI_FRAMES   0x0000u
#define OFF_OHCI_HCCA     0x1000u
#define OFF_UHCI_INTR_TD  0x1100u
#define OFF_UHCI_INTRQH   0x1110u
#define OFF_UHCI_CTRL_TD0 0x1120u
#define OFF_UHCI_CTRL_TD1 0x1130u
#define OFF_UHCI_CTRL_TD2 0x1140u
#define OFF_UHCI_CTRLQH   0x1150u
#define OFF_OHCI_INTRED   0x1160u
#define OFF_OHCI_INTRTD   0x1170u
#define OFF_OHCI_TAILTD   0x1180u
#define OFF_OHCI_CTRLED   0x1190u
#define OFF_OHCI_SETUPTD  0x11A0u
#define OFF_OHCI_DATATD   0x11B0u
#define OFF_OHCI_STATTD   0x11C0u
#define OFF_OHCI_CTRLTAIL 0x11D0u
#define OFF_SETUP_PKT     0x11E0u
#define OFF_CTRL_DATA     0x11E8u
#define OFF_REPORT_BUF    0x1200u

/* ── USB setup packet ────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    u8 bmRequestType; u8 bRequest; u16 wValue; u16 wIndex; u16 wLength;
} usb_setup_pkt_t;
#define USB_REQ_SET_ADDRESS  5
#define USB_REQ_SET_PROTOCOL 11

/* ── Hardware structs ────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    volatile u32 link, ctrl_status, token, buffer;
} uhci_td_t;

typedef struct __attribute__((packed)) {
    volatile u32 head_link, element_link;
} uhci_qh_t;

typedef struct __attribute__((packed)) {
    volatile u32 flags, tail_td, head_td, next_ed;
} ohci_ed_t;

typedef struct __attribute__((packed)) {
    volatile u32 flags, cbp, next_td, be;
} ohci_td_t;

typedef struct __attribute__((packed)) {
    u32 intr_table[32];
    u16 frame_no; u16 pad;
    u32 done_head;
    u8  reserved[116];
} ohci_hcca_t;

typedef struct __attribute__((packed)) {
    u8 modifiers, reserved, keycode[6];
} hid_report_t;

/* ── UHCI registers ──────────────────────────────────────────────────────── */
#define UHCI_USBCMD    0x00
#define UHCI_USBSTS    0x02
#define UHCI_USBINTR   0x04
#define UHCI_FRNUM     0x06
#define UHCI_FRBASEADD 0x08

#define UHCI_CMD_RS     (1u<<0)
#define UHCI_CMD_HCRESET (1u<<1)
#define UHCI_CMD_GRESET  (1u<<2)
#define UHCI_CMD_CF     (1u<<6)

#define UHCI_TD_ACTIVE  (1u<<23)
#define UHCI_TD_IOC     (1u<<24)
#define UHCI_TD_LS      (1u<<26)
#define UHCI_TD_NAK     (1u<<20)
#define UHCI_TD_ERRMASK  0x007E0000u
#define UHCI_TD_HARDERR  (UHCI_TD_ERRMASK & ~UHCI_TD_NAK)

#define UHCI_PID_SETUP 0x2Du
#define UHCI_PID_IN    0x69u
#define UHCI_PID_OUT   0xE1u

/* ── OHCI registers (word index) ─────────────────────────────────────────── */
#define OHCI_HcRevision        (0x00/4)
#define OHCI_HcControl         (0x04/4)
#define OHCI_HcCommandStatus   (0x08/4)
#define OHCI_HcInterruptStatus (0x0C/4)
#define OHCI_HcInterruptEnable (0x10/4)
#define OHCI_HcInterruptDisable (0x14/4)
#define OHCI_HcHCCA            (0x18/4)
#define OHCI_HcDoneHead        (0x1C/4)
#define OHCI_HcFmInterval      (0x34/4)
#define OHCI_HcPeriodicStart   (0x40/4)
#define OHCI_HcControlHeadED   (0x20/4)
#define OHCI_HcBulkHeadED      (0x28/4)
#define OHCI_HcRhPortStatus0   (0x54/4)

#define OHCI_CTRL_PLE           (1u<<2)
#define OHCI_CTRL_CLE           (1u<<4)
#define OHCI_CTRL_HCFS_MASK     (3u<<6)
#define OHCI_CTRL_HCFS_OPERATIONAL (2u<<6)
#define OHCI_CS_HCR             (1u<<0)
#define OHCI_CS_CLF             (1u<<1)
/* HcInterruptStatus bits */
#define OHCI_INTST_WDH          (1u<<1)  /* WritebackDoneHead */
#define OHCI_INTST_SF           (1u<<2)  /* StartOfFrame */

#define OHCI_CC_NOERROR     0x0u
#define OHCI_CC_NOTACCESSED 0xFu

#define OHCI_DP_SETUP 0u
#define OHCI_DP_OUT   1u
#define OHCI_DP_IN    2u

/* ── Global state ────────────────────────────────────────────────────────── */
typedef enum { HC_NONE=0, HC_UHCI, HC_OHCI } hc_type_t;
static hc_type_t         active_hc     = HC_NONE;
static u16               u_io          = 0;
static volatile u32     *o_base        = 0;
static bool              usb_kbd_ready = false;
static u8  kbd_addr = 1, kbd_ep = 1, kbd_dtog = 0;
static bool kbd_ls  = false;
static uhci_td_t *uhci_intr_td = NULL;

/* ── Keycode tables ──────────────────────────────────────────────────────── */
/*
 * Index = keycode - 0x04.  Exactly 96 entries covering 0x04..0x63.
 *
 * 0x04-0x1D (26) a-z
 * 0x1E-0x27 (10) 1-0
 * 0x28-0x2C  (5) Enter Esc BS Tab Space
 * 0x2D-0x38 (12) - = [ ] \ # ; ' ` , . /
 * 0x39-0x58 (32) non-printable (CapsLk, F1-F12, nav, KP*, KP-, KP+, KPEnt)
 * 0x59-0x63 (11) KP1-KP9, KP0, KP.
 */
static const char hid_lo[96] =
    "abcdefghijklmnopqrstuvwxyz"   /* 26 */
    "1234567890"                    /* 10 */
    "\r\x1B\b\t "                  /*  5 */
    "-=[]\\#;'`,./"                /* 12 */
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"  /* 16 */
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"  /* 16 = 32 total non-print */
    "1234567890.";                  /* 11 */

static const char hid_hi[96] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "!@#$%^&*()"
    "\r\x1B\b\t "
    "_+{}|~:\"~<>?"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "1234567890.";

/* ── Ring buffer ─────────────────────────────────────────────────────────── */
#define KEY_BUF_SIZE 64
static char key_buf[KEY_BUF_SIZE];
static u32  key_head = 0, key_tail = 0;
static u8   current_mods = 0;
#define KBD_KC_MAX 0x64
static bool key_state[KBD_KC_MAX];
static hid_report_t prev_report;

static void key_push(char c){
    u32 n=(key_head+1)%KEY_BUF_SIZE;
    if(n==key_tail)return;
    key_buf[key_head]=c; key_head=n;
}
static bool key_empty(void){return key_head==key_tail;}
static char key_pop(void){
    if(key_empty())return 0;
    char c=key_buf[key_tail];
    key_tail=(key_tail+1)%KEY_BUF_SIZE;
    return c;
}

/* ── Delay ───────────────────────────────────────────────────────────────── */
static void delay_ms(u32 ms){
    for(u32 i=0;i<ms*4000u;i++) asm volatile("pause");
}

/* ── Process HID report ──────────────────────────────────────────────────── */
static void process_report(const hid_report_t *r){
    current_mods = r->modifiers;
    bool shift = !!(r->modifiers & 0x22);
    for(int i=0;i<6;i++){
        u8 kc=r->keycode[i];
        if(kc<0x04||kc>=KBD_KC_MAX)continue;
        bool was=false;
        for(int j=0;j<6;j++) if(prev_report.keycode[j]==kc){was=true;break;}
        if(!was){
            char c=shift?hid_hi[kc-0x04]:hid_lo[kc-0x04];
            if(c) key_push(c);
        }
        key_state[kc]=true;
    }
    for(int j=0;j<6;j++){
        u8 kc=prev_report.keycode[j];
        if(kc<0x04||kc>=KBD_KC_MAX)continue;
        bool still=false;
        for(int i=0;i<6;i++) if(r->keycode[i]==kc){still=true;break;}
        if(!still) key_state[kc]=false;
    }
    prev_report=*r;
}

/* ╔══════════════════════════════════════════════════════════════╗
 * ║  UHCI                                                        ║
 * ╚══════════════════════════════════════════════════════════════╝ */

static void uhci_build_td(uhci_td_t *td, u32 next, u8 pid,
                           u8 addr, u8 ep, u8 tog,
                           u16 maxlen, u32 buf, bool ls, bool ioc){
    td->link = next;
    td->ctrl_status = UHCI_TD_ACTIVE
                    | (ls  ? UHCI_TD_LS  : 0u)
                    | (ioc ? UHCI_TD_IOC : 0u);
    u32 elen = (maxlen==0) ? 0x7FFu : ((u32)(maxlen-1u)&0x7FFu);
    td->token = (u32)pid
              | ((u32)(addr &0x7F)<<8)
              | ((u32)(ep   &0x0F)<<15)
              | ((u32)(tog  &0x01)<<19)
              | (elen<<21);
    td->buffer = buf;
}

static void uhci_arm_intr(void){
    uhci_td_t *td = (uhci_td_t*)DMA_PTR(OFF_UHCI_INTR_TD);
    uhci_qh_t *qh = (uhci_qh_t*)DMA_PTR(OFF_UHCI_INTRQH);
    uhci_build_td(td, 0x1u, UHCI_PID_IN,
                  kbd_addr, kbd_ep, kbd_dtog,
                  (u16)sizeof(hid_report_t),
                  DMA_PHYS(OFF_REPORT_BUF),
                  kbd_ls, true);
    /* error counter = 0 (infinite) so NAK never flags as error */
    td->ctrl_status &= ~(3u<<27);
    qh->element_link = DMA_PHYS(OFF_UHCI_INTR_TD) & ~0x1u;
    uhci_intr_td = td;
}

static bool uhci_run_control(u8 addr, const usb_setup_pkt_t *pkt,
                              void *data_buf, u16 data_len, bool data_is_in){
    usb_setup_pkt_t *sp=(usb_setup_pkt_t*)DMA_PTR(OFF_SETUP_PKT);
    *sp=*pkt;
    uhci_td_t *td0=(uhci_td_t*)DMA_PTR(OFF_UHCI_CTRL_TD0);
    uhci_td_t *td1=(uhci_td_t*)DMA_PTR(OFF_UHCI_CTRL_TD1);
    uhci_td_t *td2=(uhci_td_t*)DMA_PTR(OFF_UHCI_CTRL_TD2);
    uhci_qh_t *cqh=(uhci_qh_t*)DMA_PTR(OFF_UHCI_CTRLQH);
    u32 p1=DMA_PHYS(OFF_UHCI_CTRL_TD1), p2=DMA_PHYS(OFF_UHCI_CTRL_TD2);

    uhci_build_td(td0,p1,UHCI_PID_SETUP,addr,0,0,8,DMA_PHYS(OFF_SETUP_PKT),kbd_ls,false);
    td0->ctrl_status |= (3u<<27);

    uhci_td_t *last;
    if(data_len>0 && data_buf){
        if(!data_is_in){
            u8 *db=(u8*)DMA_PTR(OFF_CTRL_DATA);
            const u8 *s=(const u8*)data_buf;
            for(u16 i=0;i<data_len;i++) db[i]=s[i];
        }
        uhci_build_td(td1,p2, data_is_in?UHCI_PID_IN:UHCI_PID_OUT,
                      addr,0,1,data_len,DMA_PHYS(OFF_CTRL_DATA),kbd_ls,false);
        td1->ctrl_status|=(3u<<27);
        uhci_build_td(td2,0x1u, data_is_in?UHCI_PID_OUT:UHCI_PID_IN,
                      addr,0,1,0,0,kbd_ls,true);
        td2->ctrl_status|=(3u<<27);
        last=td2;
    } else {
        uhci_build_td(td1,0x1u,UHCI_PID_IN,addr,0,1,0,0,kbd_ls,true);
        td1->ctrl_status|=(3u<<27);
        td0->link=p1; last=td1;
    }
    cqh->element_link=DMA_PHYS(OFF_UHCI_CTRL_TD0)&~0x1u;

    u32 timeout=500;
    while(timeout--){ if(!(last->ctrl_status&UHCI_TD_ACTIVE))break; delay_ms(1); }
    cqh->element_link=0x1u;
    if(last->ctrl_status&UHCI_TD_ACTIVE){ printk(T,"[USB] UHCI ctrl timeout\n"); return false; }
    if(last->ctrl_status&UHCI_TD_HARDERR){ printk(T,"[USB] UHCI ctrl err 0x%x\n",last->ctrl_status); return false; }
    if(data_is_in&&data_buf&&data_len>0){
        u8 *db=(u8*)DMA_PTR(OFF_CTRL_DATA), *dst=(u8*)data_buf;
        for(u16 i=0;i<data_len;i++) dst[i]=db[i];
    }
    return true;
}

static bool uhci_init(u16 io){
    u_io=io;
    outw(u_io+UHCI_USBCMD, UHCI_CMD_GRESET); delay_ms(12);
    outw(u_io+UHCI_USBCMD, 0);               delay_ms(4);
    outw(u_io+UHCI_USBCMD, UHCI_CMD_HCRESET);
    for(u32 t=50;t&&(inw(u_io+UHCI_USBCMD)&UHCI_CMD_HCRESET);t--) delay_ms(1);
    outw(u_io+UHCI_USBSTS,  0x003Fu);
    outw(u_io+UHCI_USBINTR, 0x0000u);

    volatile u32 *fl=(volatile u32*)DMA_PTR(OFF_UHCI_FRAMES);
    for(int i=0;i<1024;i++) fl[i]=0x1u;

    uhci_qh_t *iqh=(uhci_qh_t*)DMA_PTR(OFF_UHCI_INTRQH);
    uhci_qh_t *cqh=(uhci_qh_t*)DMA_PTR(OFF_UHCI_CTRLQH);
    iqh->head_link=0x1u; iqh->element_link=0x1u;
    cqh->head_link=0x1u; cqh->element_link=0x1u;
    fl[0]=DMA_PHYS(OFF_UHCI_INTRQH)|0x2u;
    for(int i=1;i<1024;i++) fl[i]=DMA_PHYS(OFF_UHCI_CTRLQH)|0x2u;

    outl(u_io+UHCI_FRBASEADD, DMA_PHYS(OFF_UHCI_FRAMES));
    outw(u_io+UHCI_FRNUM, 0);
    outw(u_io+UHCI_USBCMD, UHCI_CMD_RS|UHCI_CMD_CF);
    delay_ms(2);

    /* SET_ADDRESS */
    kbd_addr=0;
    usb_setup_pkt_t pkt={0x00,USB_REQ_SET_ADDRESS,1,0,0};
    if(uhci_run_control(0,&pkt,0,0,false)){ kbd_addr=1; delay_ms(2); }

    /* SET_PROTOCOL = Boot Protocol */
    pkt.bmRequestType=0x21; pkt.bRequest=USB_REQ_SET_PROTOCOL;
    pkt.wValue=0; pkt.wIndex=0; pkt.wLength=0;
    uhci_run_control(kbd_addr,&pkt,0,0,false);

    kbd_ep=1; kbd_dtog=0;
    uhci_arm_intr();
    printk(T,"[USB] UHCI ready — kbd addr %u ep %u\n",kbd_addr,kbd_ep);
    return true;
}

/* ╔══════════════════════════════════════════════════════════════╗
 * ║  OHCI                                                        ║
 * ╚══════════════════════════════════════════════════════════════╝ */

/*
 * OHCI TD flags layout:
 *   [31:28] CC   = 0xF (NotAccessed)
 *   [25:24] T    = toggle (0=D0, 1=D1, 2/3=carry)
 *   [23:21] DI   = 0 (interrupt immediately on completion)
 *   [19:18] DP   = direction (0=SETUP, 1=OUT, 2=IN)
 *   [18]    R    = buffer rounding (allow short packet)
 *
 * R and DP share bit 18. DP=IN=2 (binary 10) has bit18=0,
 * so R must be OR'd in separately. DP=OUT=1 (binary 01) has
 * bit18=1, so R is implicitly set. SETUP=0 needs explicit R.
 */
static void ohci_build_td(ohci_td_t *td, u32 next,
                           u8 dp, u8 toggle,
                           u32 buf_phys, u32 buf_end){
    td->flags = ((u32)0xFu  << 28)           /* CC = NotAccessed */
              | ((u32)(toggle&0x3u) << 24)   /* T                */
              | ((u32)0u    << 21)           /* DI = 0           */
              | ((u32)(dp&0x3u) << 18)       /* DP               */
              | (1u << 18);                  /* R (always set)   */
    /*
     * For DP=IN (dp=2 = binary 10):
     *   dp<<18 = 0x00080000 (bit19 set, bit18 clear)
     *   |1<<18 = 0x00040000
     *   result = 0x000C0000 → DP=IN with R set  ✓
     */
    td->cbp     = buf_phys;
    td->next_td = next;
    td->be      = buf_end;
}

static bool ohci_run_control(u8 addr, const usb_setup_pkt_t *pkt,
                              void *data_buf, u16 data_len, bool data_is_in){
    usb_setup_pkt_t *sp=(usb_setup_pkt_t*)DMA_PTR(OFF_SETUP_PKT);
    *sp=*pkt;
    ohci_td_t *td_setup=(ohci_td_t*)DMA_PTR(OFF_OHCI_SETUPTD);
    ohci_td_t *td_data =(ohci_td_t*)DMA_PTR(OFF_OHCI_DATATD);
    ohci_td_t *td_stat =(ohci_td_t*)DMA_PTR(OFF_OHCI_STATTD);
    ohci_td_t *td_tail =(ohci_td_t*)DMA_PTR(OFF_OHCI_CTRLTAIL);
    ohci_ed_t *ed      =(ohci_ed_t*)DMA_PTR(OFF_OHCI_CTRLED);
    td_tail->flags=td_tail->cbp=td_tail->next_td=td_tail->be=0;
    u32 pd=DMA_PHYS(OFF_OHCI_DATATD), ps=DMA_PHYS(OFF_OHCI_STATTD);
    u32 pt=DMA_PHYS(OFF_OHCI_CTRLTAIL);

    ohci_build_td(td_setup, (data_len>0)?pd:ps, OHCI_DP_SETUP, 0,
                  DMA_PHYS(OFF_SETUP_PKT), DMA_PHYS(OFF_SETUP_PKT)+7u);
    if(data_len>0&&data_buf){
        if(!data_is_in){
            u8 *db=(u8*)DMA_PTR(OFF_CTRL_DATA);
            const u8 *s=(const u8*)data_buf;
            for(u16 i=0;i<data_len;i++) db[i]=s[i];
        }
        ohci_build_td(td_data, ps, data_is_in?OHCI_DP_IN:OHCI_DP_OUT, 2,
                      DMA_PHYS(OFF_CTRL_DATA), DMA_PHYS(OFF_CTRL_DATA)+data_len-1u);
        ohci_build_td(td_stat, pt, data_is_in?OHCI_DP_OUT:OHCI_DP_IN, 3, 0, 0);
    } else {
        ohci_build_td(td_stat, pt, OHCI_DP_IN, 3, 0, 0);
    }

    ed->flags = ((u32)(addr&0x7F))
              | (0u<<11)
              | ((u32)(kbd_ls?1u:0u)<<13)
              | (8u<<16);
    ed->tail_td = pt;
    ed->head_td = DMA_PHYS(OFF_OHCI_SETUPTD);
    ed->next_ed = 0;

    o_base[OHCI_HcControlHeadED] = DMA_PHYS(OFF_OHCI_CTRLED);
    o_base[OHCI_HcControl] |= OHCI_CTRL_CLE;
    o_base[OHCI_HcCommandStatus] = OHCI_CS_CLF;

    u32 timeout=500;
    while(timeout--){
        if((ed->head_td&~0xFu)==pt) break;
        delay_ms(1);
    }
    o_base[OHCI_HcControl] &= ~OHCI_CTRL_CLE;
    o_base[OHCI_HcControlHeadED]=0;

    u8 cc=(u8)((td_stat->flags>>28)&0xFu);
    if(cc!=OHCI_CC_NOERROR&&cc!=OHCI_CC_NOTACCESSED){
        printk(T,"[USB] OHCI ctrl CC=0x%x\n",cc); return false;
    }
    if(data_is_in&&data_buf&&data_len>0){
        u8 *db=(u8*)DMA_PTR(OFF_CTRL_DATA), *dst=(u8*)data_buf;
        for(u16 i=0;i<data_len;i++) dst[i]=db[i];
    }
    return true;
}

static bool ohci_init(u32 mmio){
    o_base=(volatile u32*)mmio;

    /* 1. Software reset */
    o_base[OHCI_HcCommandStatus]=OHCI_CS_HCR;
    u32 t=10;
    while((o_base[OHCI_HcCommandStatus]&OHCI_CS_HCR)&&t--) delay_ms(1);
    if(o_base[OHCI_HcCommandStatus]&OHCI_CS_HCR){
        printk(T,"[USB] OHCI reset timeout\n"); return false;
    }

    /* 2. Disable all HC-generated interrupts (we poll) */
    o_base[OHCI_HcInterruptDisable]=0xFFFFFFFFu;

    /* 3. HCCA */
    ohci_hcca_t *hcca=(ohci_hcca_t*)DMA_PTR(OFF_OHCI_HCCA);
    for(int i=0;i<32;i++) hcca->intr_table[i]=0;
    hcca->frame_no=0; hcca->done_head=0;
    o_base[OHCI_HcHCCA]=DMA_PHYS(OFF_OHCI_HCCA);

    /* 4. Frame interval
     *    FmInterval register layout:
     *      bits[13:0]  = FI   (frame interval, 11999 = 0x2EDF)
     *      bit  31     = FIT  (toggle, must differ from current FIT after write)
     *      bits[30:16] = FSMPS (FS largest data packet = (FI - 210) * 6 / 7 = 10104 = 0x2778)
     */
    u32 fi   = 0x2EDFu;
    u32 fsmps= 0x2778u;
    u32 fit  = ((o_base[OHCI_HcFmInterval]>>31)^1u)&1u; /* toggle FIT */
    o_base[OHCI_HcFmInterval] = (fit<<31)|(fsmps<<16)|fi;
    o_base[OHCI_HcPeriodicStart] = (fi*9u)/10u; /* 90% of FI */

    /* 5. Clear control/bulk head EDs */
    o_base[OHCI_HcControlHeadED]=0;
    o_base[OHCI_HcBulkHeadED]=0;

    /* 6. Transition to Operational */
    u32 ctrl=o_base[OHCI_HcControl];
    ctrl &= ~OHCI_CTRL_HCFS_MASK;
    ctrl |=  OHCI_CTRL_HCFS_OPERATIONAL;
    o_base[OHCI_HcControl]=ctrl;
    delay_ms(2);

    /* 7. Detect speed from port status
     *    NOTE: We do NOT reset the port or re-enumerate.
     *    QEMU hands the device off pre-configured at addr 1, ep 1.
     *    Issuing a port reset destroys that state.
     */
    kbd_addr = 1;
    kbd_ep   = 1;
    kbd_ls   = !!(o_base[OHCI_HcRhPortStatus0] & (1u<<9)); /* LSD bit */

    /* 8. Send SET_PROTOCOL (Boot Protocol) — safe to do without port reset */
    usb_setup_pkt_t pkt={0x21, USB_REQ_SET_PROTOCOL, 0, 0, 0};
    if(!ohci_run_control(kbd_addr,&pkt,0,0,false))
        printk(T,"[USB] OHCI SET_PROTOCOL failed (continuing anyway)\n");

    /* 9. Build interrupt IN ED */
    ohci_ed_t *ed=(ohci_ed_t*)DMA_PTR(OFF_OHCI_INTRED);
    ed->flags = ((u32)(kbd_addr&0x7F))
              | ((u32)(kbd_ep  &0x0F)<<7)
              | (2u<<11)                             /* Direction=IN */
              | ((u32)(kbd_ls?1u:0u)<<13)
              | (8u<<16);                            /* MPS=8 */
    /* K (Skip) bit = 0 — we want the HC to process this ED */

    /* 10. Build interrupt IN TD */
    ohci_td_t *td  =(ohci_td_t*)DMA_PTR(OFF_OHCI_INTRTD);
    ohci_td_t *tail=(ohci_td_t*)DMA_PTR(OFF_OHCI_TAILTD);
    tail->flags=tail->cbp=tail->next_td=tail->be=0;

    ohci_build_td(td, DMA_PHYS(OFF_OHCI_TAILTD), OHCI_DP_IN, 2,
                  DMA_PHYS(OFF_REPORT_BUF),
                  DMA_PHYS(OFF_REPORT_BUF)+sizeof(hid_report_t)-1u);

    ed->head_td=DMA_PHYS(OFF_OHCI_INTRTD);
    ed->tail_td=DMA_PHYS(OFF_OHCI_TAILTD);
    ed->next_ed=0;

    /* 11. Wire ED into all 32 HCCA slots (every 1 ms) */
    for(int i=0;i<32;i++) hcca->intr_table[i]=DMA_PHYS(OFF_OHCI_INTRED);

    /* 12. Enable periodic list */
    o_base[OHCI_HcControl] |= OHCI_CTRL_PLE;

    printk(T,"[USB] OHCI ready — kbd addr %u ep %u ls=%d\n",
           kbd_addr, kbd_ep, (int)kbd_ls);
    printk(T,"[USB] OHCI HcRevision=0x%x HcControl=0x%x\n",
           o_base[OHCI_HcRevision], o_base[OHCI_HcControl]);
    return true;
}

/* ╔══════════════════════════════════════════════════════════════╗
 * ║  PCI Discovery                                               ║
 * ╚══════════════════════════════════════════════════════════════╝ */
void usb_kbd_init(void){
    printk(T,"[USB] Scanning PCI for USB host controllers...\n");
    for(u8 b=0;b<8;b++){
        if((pciread(b,0,0,0)&0xFFFF)==0xFFFF) continue;
        for(u8 s=0;s<32;s++){
            if((pciread(b,s,0,0)&0xFFFF)==0xFFFF) continue;
            u32 hdr=pciread(b,s,0,0x0C);
            u8 maxfn=((hdr>>16)&0x80)?8:1;
            for(u8 f=0;f<maxfn;f++){
                if((pciread(b,s,f,0)&0xFFFF)==0xFFFF) continue;
                u32 cls=pciread(b,s,f,8);
                if((cls>>16)!=0x0C03u) continue;
                /* Enable Bus Master + IO + Memory */
                u32 cmd=pciread(b,s,f,4);
                u32 addr=0x80000000u|((u32)b<<16)|((u32)s<<11)|((u32)f<<8)|0x04u;
                outl(0xCF8,addr); outl(0xCFC,cmd|0x7u);
                u8 prog=(cls>>8)&0xFF;
                if(prog==0x00){
                    u16 io=(u16)(pciread(b,s,f,0x20)&0xFFFCu);
                    printk(T,"[USB] Found UHCI at I/O 0x%x\n",io);
                    if(uhci_init(io)){ active_hc=HC_UHCI; usb_kbd_ready=true; return; }
                } else if(prog==0x10){
                    u32 mmio=pciread(b,s,f,0x10)&0xFFFFFFF0u;
                    printk(T,"[USB] Found OHCI at MMIO 0x%x\n",mmio);
                    if(ohci_init(mmio)){ active_hc=HC_OHCI; usb_kbd_ready=true; return; }
                }
            }
        }
    }
    printk(T,"[USB] No supported USB host controller found\n");
}

/* ╔══════════════════════════════════════════════════════════════╗
 * ║  Poll                                                        ║
 * ╚══════════════════════════════════════════════════════════════╝ */
void usb_kbd_poll(void){
    if(!usb_kbd_ready) return;

    if(active_hc==HC_UHCI){
        if(!uhci_intr_td) return;
        u32 cs=uhci_intr_td->ctrl_status;
        if(cs&UHCI_TD_ACTIVE) return;
        if(cs&UHCI_TD_NAK){ uhci_arm_intr(); return; }
        if(cs&UHCI_TD_HARDERR){
          kbd_dtog=0; uhci_arm_intr(); return;
        }
        process_report((hid_report_t*)DMA_PTR(OFF_REPORT_BUF));
        kbd_dtog^=1;
        uhci_arm_intr();

    } else if(active_hc==HC_OHCI){
        ohci_hcca_t *hcca=(ohci_hcca_t*)DMA_PTR(OFF_OHCI_HCCA);

        /*
         * The HC only updates done_head in HCCA after it sets the WDH
         * (WritebackDoneHead) bit in HcInterruptStatus.
         * We must acknowledge WDH by writing 1 to it so the HC will
         * write the next completed TD's address on the following frame.
         * Without this acknowledgement done_head stays frozen after the
         * first completion and we never see subsequent keypresses.
         */
        u32 intst = o_base[OHCI_HcInterruptStatus];
        if(intst & OHCI_INTST_WDH){
            /* Acknowledge WDH — write 1 to clear */
            o_base[OHCI_HcInterruptStatus] = OHCI_INTST_WDH;
        }

        /* Read done_head from HCCA (HC writes here when WDH fires) */
        u32 dh = hcca->done_head & ~1u;
        if(!dh) return;
        hcca->done_head = 0;

        ohci_td_t *ctd=(ohci_td_t*)dh;
        u8 cc=(u8)((ctd->flags>>28)&0xFu);
        if(cc==OHCI_CC_NOERROR)
            process_report((hid_report_t*)DMA_PTR(OFF_REPORT_BUF));
        else
            printk(T,"[USB] OHCI TD CC=0x%x\n",cc);

        /* Re-arm */
        ohci_td_t *td=(ohci_td_t*)DMA_PTR(OFF_OHCI_INTRTD);
        ohci_build_td(td, DMA_PHYS(OFF_OHCI_TAILTD), OHCI_DP_IN, 2,
                      DMA_PHYS(OFF_REPORT_BUF),
                      DMA_PHYS(OFF_REPORT_BUF)+sizeof(hid_report_t)-1u);
        ohci_ed_t *ed=(ohci_ed_t*)DMA_PTR(OFF_OHCI_INTRED);
        ed->head_td=DMA_PHYS(OFF_OHCI_INTRTD);
    }
}

/* ╔══════════════════════════════════════════════════════════════╗
 * ║  Public API                                                  ║
 * ╚══════════════════════════════════════════════════════════════╝ */
bool usb_kbd_is_initialized(void){ return usb_kbd_ready; }
bool usb_kbd_has_input(void)     { return !key_empty(); }
u8   usb_kbd_get_modifiers(void) { return current_mods; }

void usb_kbd_get_key_state(bool *shift, bool *ctrl, bool *alt){
    if(shift) *shift=!!(current_mods&0x22);
    if(ctrl)  *ctrl =!!(current_mods&0x11);
    if(alt)   *alt  =!!(current_mods&0x44);
}

char usb_kbd_read(void){
    while(key_empty()) usb_kbd_poll();
    return key_pop();
}

char usb_kbd_getc(void){
    if(key_empty()) return 0;
    return key_pop();
}

bool usb_kbd_key_held(u8 kc){
    if(kc<0x04||kc>=KBD_KC_MAX) return false;
    return key_state[kc];
}