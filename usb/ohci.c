/*
 * usb/ohci.c — Open Host Controller Interface (USB 1.1)
 *
 * OHCI is the rival USB 1.1 spec by Compaq/Microsoft/National Semiconductor.
 * Unlike UHCI (I/O port registers), OHCI uses MMIO registers and a hardware-
 * managed Host Controller Communications Area (HCCA) in physical memory.
 *
 * Key concepts:
 *   Endpoint Descriptor (ED) : 16 bytes — points to a TD list, queued on HC
 *   Transfer Descriptor (TD) : 16 bytes — one USB transaction
 *   HCCA                     : 256 bytes — shared memory block, HC writes here
 *
 * MMIO register map (base + word index × 4):
 *   0x00  HcRevision
 *   0x04  HcControl         — HCFS state machine, list enables
 *   0x08  HcCommandStatus   — HCR reset, CLF/BLF fill flags
 *   0x0C  HcInterruptStatus — WDH (WritebackDoneHead), SF (StartOfFrame)
 *   0x10  HcInterruptEnable
 *   0x14  HcInterruptDisable
 *   0x18  HcHCCA            — physical address of HCCA block
 *   0x1C  HcDoneHead        — last completed TD (read-only)
 *   0x20  HcControlHeadED   — head of control ED list
 *   0x28  HcBulkHeadED      — head of bulk ED list
 *   0x34  HcFmInterval      — frame timing (FI=11999, FSMPS=10104)
 *   0x40  HcPeriodicStart   — 90% of FmInterval
 *   0x54  HcRhPortStatus[0] — root hub port 0
 *
 * Ark integration:
 *   ohci_init(mmio_base)  — reset, configure, go Operational
 *   ohci_kbd_poll()       — check HCCA done_head, decode HID boot report
 */
#include "ark/types.h"
#include "ark/printk.h"

/* ── MMIO helpers ─────────────────────────────────────────────────── */
#define OHCI_R(base, reg)      (((volatile u32*)(base))[(reg)/4])
#define OHCI_W(base, reg, val) (((volatile u32*)(base))[(reg)/4] = (val))

/* ── Register offsets ─────────────────────────────────────────────── */
#define OHCI_HcRevision        0x00
#define OHCI_HcControl         0x04
#define OHCI_HcCommandStatus   0x08
#define OHCI_HcInterruptStatus 0x0C
#define OHCI_HcInterruptDisable 0x14
#define OHCI_HcHCCA            0x18
#define OHCI_HcDoneHead        0x1C
#define OHCI_HcControlHeadED   0x20
#define OHCI_HcBulkHeadED      0x28
#define OHCI_HcFmInterval      0x34
#define OHCI_HcPeriodicStart   0x40
#define OHCI_HcRhPortStatus0   0x54

#define OHCI_CTRL_HCFS_MASK        (3u<<6)
#define OHCI_CTRL_HCFS_OPERATIONAL (2u<<6)
#define OHCI_CTRL_PLE              (1u<<2)   /* Periodic List Enable  */
#define OHCI_CTRL_CLE              (1u<<4)   /* Control List Enable   */
#define OHCI_CS_HCR                (1u<<0)   /* Host Controller Reset */
#define OHCI_CS_CLF                (1u<<1)   /* Control List Filled   */
#define OHCI_INTST_WDH             (1u<<1)   /* WritebackDoneHead     */

/* ── Data structures ─────────────────────────────────────────────── */
typedef struct __attribute__((packed)) { volatile u32 flags, tail_td, head_td, next_ed; } ohci_ed_t;
typedef struct __attribute__((packed)) { volatile u32 flags, cbp, next_td, be; } ohci_td_t;
typedef struct __attribute__((packed)) {
    u32 intr_table[32]; u16 frame_no; u16 pad; u32 done_head; u8 reserved[116];
} ohci_hcca_t;
typedef struct __attribute__((packed)) {
    u8 modifiers, reserved, keycode[6];
} ohci_hid_report_t;

/* ── DMA pool ────────────────────────────────────────────────────── */
#define OHCI_DMA_SZ  0x2000
__attribute__((aligned(4096))) static u8 ohci_dma[OHCI_DMA_SZ];
#define OHCI_PHYS(off) ((u32)((usize)ohci_dma + (usize)(off)))
#define OHCI_PTR(off)  ((void*)((usize)ohci_dma + (usize)(off)))

#define OFF_HCCA      0x0000u
#define OFF_INTRED    0x0100u
#define OFF_INTRTD    0x0110u
#define OFF_TAILTD    0x0120u
#define OFF_CTRLED    0x0130u
#define OFF_SETUPTD   0x0140u
#define OFF_DATATD    0x0150u
#define OFF_STATTD    0x0160u
#define OFF_CTRLTAIL  0x0170u
#define OFF_SETUP_PKT 0x0180u
#define OFF_CTRL_DATA 0x0190u
#define OFF_REPORT    0x0200u

/* TD flag helpers */
#define OHCI_DP_SETUP 0u
#define OHCI_DP_OUT   1u
#define OHCI_DP_IN    2u

static void ohci_build_td(ohci_td_t *td, u32 next, u8 dp, u8 tog, u32 buf, u32 end) {
    td->flags = (0xFu<<28) | ((u32)(tog&3u)<<24) | ((u32)(dp&3u)<<18) | (1u<<18);
    td->cbp     = buf;
    td->next_td = next;
    td->be      = end;
}

/* ── State ───────────────────────────────────────────────────────── */
static usize ohci_base = 0;
static bool  ohci_ready = false;
static u8    ohci_kbd_addr = 1, ohci_kbd_ep = 1;
static bool  ohci_kbd_ls = false;

#define OHCI_KEY_BUF 64
static char ohci_key_buf[OHCI_KEY_BUF];
static u32  ohci_khead = 0, ohci_ktail = 0;
static ohci_hid_report_t ohci_prev_report;
static const char ohci_hid_lo[96] =
    "abcdefghijklmnopqrstuvwxyz1234567890\r\x1B\b\t -=[]\\#;'`,./"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "1234567890.";
static const char ohci_hid_hi[96] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\x1B\b\t _+{}|~:\"~<>?"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "1234567890.";

static void ohci_key_push(char c){ u32 n=(ohci_khead+1)%OHCI_KEY_BUF; if(n!=ohci_ktail){ohci_key_buf[ohci_khead]=c;ohci_khead=n;} }
bool ohci_kbd_has_input(void){ return ohci_khead!=ohci_ktail; }
char ohci_kbd_getc(void){
    if(!ohci_kbd_has_input())return 0;
    char c=ohci_key_buf[ohci_ktail]; ohci_ktail=(ohci_ktail+1)%OHCI_KEY_BUF; return c;
}

static void ohci_delay(u32 ms){ for(u32 i=0;i<ms*4000u;i++) __asm__ volatile("pause"); }

typedef struct __attribute__((packed)){u8 bmRT,bReq;u16 wVal,wIdx,wLen;} ohci_setup_pkt_t;

static bool ohci_control(u8 addr, const ohci_setup_pkt_t *pkt, void *data, u16 dlen, bool in) {
    ohci_setup_pkt_t *sp = (ohci_setup_pkt_t*)OHCI_PTR(OFF_SETUP_PKT);
    *sp = *pkt;
    ohci_td_t *tds  = (ohci_td_t*)OHCI_PTR(OFF_SETUPTD);
    ohci_td_t *tdd  = (ohci_td_t*)OHCI_PTR(OFF_DATATD);
    ohci_td_t *tdst = (ohci_td_t*)OHCI_PTR(OFF_STATTD);
    ohci_td_t *tail = (ohci_td_t*)OHCI_PTR(OFF_CTRLTAIL);
    ohci_ed_t *ed   = (ohci_ed_t*)OHCI_PTR(OFF_CTRLED);
    tail->flags=tail->cbp=tail->next_td=tail->be=0;
    u32 pds=OHCI_PHYS(OFF_DATATD), pdst=OHCI_PHYS(OFF_STATTD), pt=OHCI_PHYS(OFF_CTRLTAIL);
    ohci_build_td(tds, dlen>0?pds:pdst, OHCI_DP_SETUP, 0,
                  OHCI_PHYS(OFF_SETUP_PKT), OHCI_PHYS(OFF_SETUP_PKT)+7u);
    if (dlen > 0 && data) {
        if (!in) { u8*d=(u8*)OHCI_PTR(OFF_CTRL_DATA),*s=(u8*)data; for(u16 i=0;i<dlen;i++)d[i]=s[i]; }
        ohci_build_td(tdd, pdst, in?OHCI_DP_IN:OHCI_DP_OUT, 2, OHCI_PHYS(OFF_CTRL_DATA), OHCI_PHYS(OFF_CTRL_DATA)+dlen-1u);
        ohci_build_td(tdst, pt, in?OHCI_DP_OUT:OHCI_DP_IN, 3, 0, 0);
    } else { ohci_build_td(tdst, pt, OHCI_DP_IN, 3, 0, 0); }
    ed->flags = (u32)(addr&0x7F)|((u32)(ohci_kbd_ls?1u:0u)<<13)|(8u<<16);
    ed->tail_td = pt; ed->head_td = OHCI_PHYS(OFF_SETUPTD); ed->next_ed = 0;
    OHCI_W(ohci_base, OHCI_HcControlHeadED, OHCI_PHYS(OFF_CTRLED));
    OHCI_W(ohci_base, OHCI_HcControl, OHCI_R(ohci_base, OHCI_HcControl)|OHCI_CTRL_CLE);
    OHCI_W(ohci_base, OHCI_HcCommandStatus, OHCI_CS_CLF);
    u32 t=500; while(t-- && (ed->head_td&~0xFu)!=pt) ohci_delay(1);
    OHCI_W(ohci_base, OHCI_HcControl, OHCI_R(ohci_base,OHCI_HcControl)&~OHCI_CTRL_CLE);
    OHCI_W(ohci_base, OHCI_HcControlHeadED, 0);
    if (in&&data&&dlen>0){ u8*s=(u8*)OHCI_PTR(OFF_CTRL_DATA),*d=(u8*)data; for(u16 i=0;i<dlen;i++)d[i]=s[i]; }
    return true;
}

bool ohci_init(u32 mmio) {
    ohci_base = (usize)mmio;
    OHCI_W(ohci_base, OHCI_HcCommandStatus, OHCI_CS_HCR);
    u32 t=10; while((OHCI_R(ohci_base,OHCI_HcCommandStatus)&OHCI_CS_HCR)&&t--) ohci_delay(1);
    OHCI_W(ohci_base, OHCI_HcInterruptDisable, 0xFFFFFFFFu);

    ohci_hcca_t *hcca = (ohci_hcca_t*)OHCI_PTR(OFF_HCCA);
    for(int i=0;i<32;i++) hcca->intr_table[i]=0;
    hcca->frame_no=0; hcca->done_head=0;
    OHCI_W(ohci_base, OHCI_HcHCCA, OHCI_PHYS(OFF_HCCA));

    u32 fi=0x2EDFu, fsmps=0x2778u;
    u32 fit=((OHCI_R(ohci_base,OHCI_HcFmInterval)>>31)^1u)&1u;
    OHCI_W(ohci_base, OHCI_HcFmInterval, (fit<<31)|(fsmps<<16)|fi);
    OHCI_W(ohci_base, OHCI_HcPeriodicStart, (fi*9u)/10u);
    OHCI_W(ohci_base, OHCI_HcControlHeadED, 0);
    OHCI_W(ohci_base, OHCI_HcBulkHeadED, 0);

    u32 ctrl = OHCI_R(ohci_base, OHCI_HcControl);
    ctrl = (ctrl & ~OHCI_CTRL_HCFS_MASK) | OHCI_CTRL_HCFS_OPERATIONAL;
    OHCI_W(ohci_base, OHCI_HcControl, ctrl);
    ohci_delay(2);

    ohci_kbd_addr=1; ohci_kbd_ep=1;
    ohci_kbd_ls = !!(OHCI_R(ohci_base, OHCI_HcRhPortStatus0) & (1u<<9));

    ohci_setup_pkt_t pkt={0x21,11,0,0,0}; /* SET_PROTOCOL = boot */
    ohci_control(ohci_kbd_addr, &pkt, 0, 0, false);

    ohci_ed_t *ed  = (ohci_ed_t*)OHCI_PTR(OFF_INTRED);
    ohci_td_t *td  = (ohci_td_t*)OHCI_PTR(OFF_INTRTD);
    ohci_td_t *tail= (ohci_td_t*)OHCI_PTR(OFF_TAILTD);
    tail->flags=tail->cbp=tail->next_td=tail->be=0;
    ohci_build_td(td, OHCI_PHYS(OFF_TAILTD), OHCI_DP_IN, 2,
                  OHCI_PHYS(OFF_REPORT), OHCI_PHYS(OFF_REPORT)+sizeof(ohci_hid_report_t)-1u);
    ed->flags = (u32)(ohci_kbd_addr&0x7F)|((u32)(ohci_kbd_ep&0xFu)<<7)|(2u<<11)|((u32)(ohci_kbd_ls?1u:0u)<<13)|(8u<<16);
    ed->head_td=OHCI_PHYS(OFF_INTRTD); ed->tail_td=OHCI_PHYS(OFF_TAILTD); ed->next_ed=0;
    for(int i=0;i<32;i++) hcca->intr_table[i]=OHCI_PHYS(OFF_INTRED);
    OHCI_W(ohci_base, OHCI_HcControl, OHCI_R(ohci_base,OHCI_HcControl)|OHCI_CTRL_PLE);
    ohci_ready=true;
    printk(T,"[OHCI] kbd ready addr=%u ep=%u ls=%d rev=0x%x\n",
           ohci_kbd_addr, ohci_kbd_ep, (int)ohci_kbd_ls,
           OHCI_R(ohci_base,OHCI_HcRevision));
    return true;
}

static void ohci_process_report(const ohci_hid_report_t *r){
    bool shift=!!(r->modifiers&0x22u);
    for(int i=0;i<6;i++){
        u8 kc=r->keycode[i]; if(kc<4||kc>=0x64)continue;
        bool was=false; for(int j=0;j<6;j++) if(ohci_prev_report.keycode[j]==kc){was=true;break;}
        if(!was){ char c=shift?ohci_hid_hi[kc-4]:ohci_hid_lo[kc-4]; if(c)ohci_key_push(c); }
    }
    ohci_prev_report=*r;
}

void ohci_kbd_poll(void){
    if(!ohci_ready)return;
    u32 intst=OHCI_R(ohci_base,OHCI_HcInterruptStatus);
    if(intst&OHCI_INTST_WDH) OHCI_W(ohci_base,OHCI_HcInterruptStatus,OHCI_INTST_WDH);
    ohci_hcca_t *hcca=(ohci_hcca_t*)OHCI_PTR(OFF_HCCA);
    u32 dh=hcca->done_head&~1u; if(!dh)return; hcca->done_head=0;
    ohci_td_t *ctd=(ohci_td_t*)(usize)dh;
    u8 cc=(u8)((ctd->flags>>28)&0xFu);
    if(cc==0) ohci_process_report((ohci_hid_report_t*)OHCI_PTR(OFF_REPORT));
    ohci_td_t *td=(ohci_td_t*)OHCI_PTR(OFF_INTRTD);
    ohci_build_td(td,OHCI_PHYS(OFF_TAILTD),OHCI_DP_IN,2,
                  OHCI_PHYS(OFF_REPORT),OHCI_PHYS(OFF_REPORT)+sizeof(ohci_hid_report_t)-1u);
    ohci_ed_t *ed=(ohci_ed_t*)OHCI_PTR(OFF_INTRED);
    ed->head_td=OHCI_PHYS(OFF_INTRTD);
}

bool ohci_kbd_ready(void){ return ohci_ready; }
