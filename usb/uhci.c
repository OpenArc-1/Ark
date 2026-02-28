/*
 * usb/uhci.c — Universal Host Controller Interface (USB 1.1)
 *
 * UHCI is Intel's original USB 1.1 host controller (12 Mbit/s full-speed,
 * 1.5 Mbit/s low-speed). Uses I/O ports and a software-managed 1024-entry
 * frame list in physical memory. Each frame points to a queue of TDs.
 *
 * Register map (I/O base + offset):
 *   +0x00  USBCMD   — Run/Stop, Host Reset, Global Reset, Configure Flag
 *   +0x02  USBSTS   — interrupt status (write 1 to clear each bit)
 *   +0x04  USBINTR  — interrupt enable mask
 *   +0x06  FRNUM    — current frame number (10-bit)
 *   +0x08  FRBASEADD — frame list base address (4KB-aligned physical)
 *   +0x0C  SOFMOD   — Start-Of-Frame Modify
 *   +0x10  PORTSC0  — port 0 status/control
 *   +0x12  PORTSC1  — port 1 status/control
 *
 * Data structures (all in physical / identity-mapped memory):
 *   Frame list  : 1024 × u32 — each entry is a TD or QH physical pointer
 *   Queue Head  : 8 bytes    — head_link | element_link
 *   Transfer Descriptor : 16 bytes — link | ctrl_status | token | buffer
 *
 * Ark integration:
 *   uhci_init(io_base)     — reset controller, build frame list, start HC
 *   uhci_kbd_init(io_base) — enumerate keyboard on port 0 or 1 at addr 1
 *   uhci_kbd_poll()        — check interrupt TD, decode HID boot report
 */
#include "ark/types.h"
#include "ark/printk.h"
#include "ark/pci.h"

/* ── Port I/O ──────────────────────────────────────────────────────── */
static inline void _uhci_outb(u16 p,u8  v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void _uhci_outw(u16 p,u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void _uhci_outl(u16 p,u32 v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline u8   _uhci_inb(u16 p){u8  v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u16  _uhci_inw(u16 p){u16 v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}

/* ── Registers ─────────────────────────────────────────────────────── */
#define UHCI_USBCMD     0x00
#define UHCI_USBSTS     0x02
#define UHCI_USBINTR    0x04
#define UHCI_FRNUM      0x06
#define UHCI_FRBASEADD  0x08
#define UHCI_SOFMOD     0x0C
#define UHCI_PORTSC0    0x10
#define UHCI_PORTSC1    0x12

#define UHCI_CMD_RS     (1u<<0)   /* Run/Stop      */
#define UHCI_CMD_HCRESET (1u<<1)  /* Host Reset    */
#define UHCI_CMD_GRESET  (1u<<2)  /* Global Reset  */
#define UHCI_CMD_CF     (1u<<6)   /* Configure Flag*/

/* ── TD / QH structures ────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    volatile u32 link;
    volatile u32 ctrl_status;
    volatile u32 token;
    volatile u32 buffer;
} uhci_td_t;

typedef struct __attribute__((packed)) {
    volatile u32 head_link;
    volatile u32 element_link;
} uhci_qh_t;

/* TD ctrl_status bits */
#define TD_ACTIVE  (1u<<23)
#define TD_IOC     (1u<<24)
#define TD_LS      (1u<<26)
#define TD_NAK     (1u<<20)
#define TD_ERRMASK 0x007E0000u

/* PID tokens */
#define PID_SETUP 0x2Du
#define PID_IN    0x69u
#define PID_OUT   0xE1u

/* ── DMA pool (8KB, 4KB-aligned, stays in low physical memory) ───── */
#define UHCI_DMA_SZ  0x2000
__attribute__((aligned(4096))) static u8 uhci_dma[UHCI_DMA_SZ];

#define UHCI_OFF_FRAMES  0x0000u   /* 1024 × u32 = 4KB  */
#define UHCI_OFF_INTRQH  0x1000u   /* interrupt QH      */
#define UHCI_OFF_CTRLQH  0x1010u   /* control QH        */
#define UHCI_OFF_INTRTD  0x1020u   /* interrupt IN TD   */
#define UHCI_OFF_CTRL0   0x1030u   /* control TD[0]     */
#define UHCI_OFF_CTRL1   0x1040u   /* control TD[1]     */
#define UHCI_OFF_CTRL2   0x1050u   /* control TD[2]     */
#define UHCI_OFF_SETUP   0x1060u   /* setup packet (8B) */
#define UHCI_OFF_CTRLDAT 0x1070u   /* control data buf  */
#define UHCI_OFF_REPORT  0x1080u   /* HID report buf    */

#define UHCI_PHYS(off)  ((u32)((usize)uhci_dma + (usize)(off)))
#define UHCI_PTR(off)   ((void*)((usize)uhci_dma + (usize)(off)))

/* ── HID boot-protocol report ──────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    u8 modifiers, reserved, keycode[6];
} uhci_hid_report_t;

/* ── State ─────────────────────────────────────────────────────────── */
static u16  uhci_io     = 0;
static bool uhci_ready  = false;
static u8   kbd_addr    = 1;
static u8   kbd_ep      = 1;
static u8   kbd_dtog    = 0;
static bool kbd_ls      = false;
static uhci_td_t *intr_td = 0;

/* Keycode → ASCII (index = hid_keycode - 0x04) */
static const char uhci_hid_lo[96] =
    "abcdefghijklmnopqrstuvwxyz1234567890\r\x1B\b\t "
    "-=[]\\#;'`,./"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "1234567890.";
static const char uhci_hid_hi[96] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\x1B\b\t "
    "_+{}|~:\"~<>?"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "1234567890.";

#define UHCI_KEY_BUF 64
static char uhci_key_buf[UHCI_KEY_BUF];
static u32  uhci_khead = 0, uhci_ktail = 0;
static uhci_hid_report_t uhci_prev_report;

static void uhci_key_push(char c){
    u32 n=(uhci_khead+1)%UHCI_KEY_BUF;
    if(n==uhci_ktail)return;
    uhci_key_buf[uhci_khead]=c; uhci_khead=n;
}
static bool uhci_key_empty(void){return uhci_khead==uhci_ktail;}
char uhci_kbd_getc(void){
    if(uhci_key_empty())return 0;
    char c=uhci_key_buf[uhci_ktail];
    uhci_ktail=(uhci_ktail+1)%UHCI_KEY_BUF;
    return c;
}
bool uhci_kbd_has_input(void){return !uhci_key_empty();}

static void uhci_delay(u32 ms){
    for(u32 i=0;i<ms*4000u;i++) __asm__ volatile("pause");
}

static void uhci_build_td(uhci_td_t *td, u32 next, u8 pid,
                           u8 addr, u8 ep, u8 tog,
                           u16 maxlen, u32 buf, bool ls, bool ioc) {
    td->link = next;
    td->ctrl_status = TD_ACTIVE
                    | (ls  ? TD_LS  : 0u)
                    | (ioc ? TD_IOC : 0u);
    u32 elen = (maxlen == 0) ? 0x7FFu : (u32)((maxlen - 1u) & 0x7FFu);
    td->token = (u32)pid
              | ((u32)(addr & 0x7Fu) << 8)
              | ((u32)(ep   & 0x0Fu) << 15)
              | ((u32)(tog  & 0x01u) << 19)
              | (elen << 21);
    td->buffer = buf;
}

static void uhci_arm_intr(void) {
    uhci_td_t *td = (uhci_td_t*)UHCI_PTR(UHCI_OFF_INTRTD);
    uhci_qh_t *qh = (uhci_qh_t*)UHCI_PTR(UHCI_OFF_INTRQH);
    uhci_build_td(td, 0x1u, PID_IN, kbd_addr, kbd_ep, kbd_dtog,
                  (u16)sizeof(uhci_hid_report_t),
                  UHCI_PHYS(UHCI_OFF_REPORT), kbd_ls, true);
    td->ctrl_status &= ~(3u << 27);   /* error counter = 0 (infinite) */
    qh->element_link = UHCI_PHYS(UHCI_OFF_INTRTD) & ~0x1u;
    intr_td = td;
}

typedef struct __attribute__((packed)) {
    u8 bmRequestType, bRequest; u16 wValue, wIndex, wLength;
} uhci_setup_pkt_t;

static bool uhci_control(u8 addr, const uhci_setup_pkt_t *pkt,
                          void *data, u16 dlen, bool in) {
    uhci_setup_pkt_t *sp = (uhci_setup_pkt_t*)UHCI_PTR(UHCI_OFF_SETUP);
    *sp = *pkt;
    uhci_td_t *td0 = (uhci_td_t*)UHCI_PTR(UHCI_OFF_CTRL0);
    uhci_td_t *td1 = (uhci_td_t*)UHCI_PTR(UHCI_OFF_CTRL1);
    uhci_td_t *td2 = (uhci_td_t*)UHCI_PTR(UHCI_OFF_CTRL2);
    uhci_qh_t *cqh = (uhci_qh_t*)UHCI_PTR(UHCI_OFF_CTRLQH);

    uhci_build_td(td0, UHCI_PHYS(UHCI_OFF_CTRL1), PID_SETUP, addr, 0, 0,
                  8, UHCI_PHYS(UHCI_OFF_SETUP), kbd_ls, false);
    td0->ctrl_status |= (3u << 27);

    uhci_td_t *last;
    if (dlen > 0 && data) {
        if (!in) {
            u8 *dst = (u8*)UHCI_PTR(UHCI_OFF_CTRLDAT);
            u8 *src = (u8*)data;
            for (u16 i = 0; i < dlen; i++) dst[i] = src[i];
        }
        uhci_build_td(td1, UHCI_PHYS(UHCI_OFF_CTRL2),
                      in ? PID_IN : PID_OUT, addr, 0, 1,
                      dlen, UHCI_PHYS(UHCI_OFF_CTRLDAT), kbd_ls, false);
        td1->ctrl_status |= (3u << 27);
        uhci_build_td(td2, 0x1u,
                      in ? PID_OUT : PID_IN, addr, 0, 1,
                      0, 0, kbd_ls, true);
        td2->ctrl_status |= (3u << 27);
        last = td2;
    } else {
        uhci_build_td(td1, 0x1u, PID_IN, addr, 0, 1, 0, 0, kbd_ls, true);
        td1->ctrl_status |= (3u << 27);
        td0->link = UHCI_PHYS(UHCI_OFF_CTRL1);
        last = td1;
    }
    cqh->element_link = UHCI_PHYS(UHCI_OFF_CTRL0) & ~0x1u;

    u32 t = 500;
    while (t-- && (last->ctrl_status & TD_ACTIVE)) uhci_delay(1);
    cqh->element_link = 0x1u;
    if (last->ctrl_status & TD_ACTIVE)  return false;
    if (last->ctrl_status & TD_ERRMASK) return false;
    if (in && data && dlen > 0) {
        u8 *src = (u8*)UHCI_PTR(UHCI_OFF_CTRLDAT);
        u8 *dst = (u8*)data;
        for (u16 i = 0; i < dlen; i++) dst[i] = src[i];
    }
    return true;
}

bool uhci_init(u16 io) {
    uhci_io = io;
    _uhci_outw(io + UHCI_USBCMD, UHCI_CMD_GRESET); uhci_delay(12);
    _uhci_outw(io + UHCI_USBCMD, 0);               uhci_delay(4);
    _uhci_outw(io + UHCI_USBCMD, UHCI_CMD_HCRESET);
    for (u32 t = 50; t && (_uhci_inw(io+UHCI_USBCMD) & UHCI_CMD_HCRESET); t--)
        uhci_delay(1);
    _uhci_outw(io + UHCI_USBSTS,  0x003Fu);
    _uhci_outw(io + UHCI_USBINTR, 0x0000u);

    /* Frame list: all entries terminate */
    volatile u32 *fl = (volatile u32*)UHCI_PTR(UHCI_OFF_FRAMES);
    for (int i = 0; i < 1024; i++) fl[i] = 0x1u;

    uhci_qh_t *iqh = (uhci_qh_t*)UHCI_PTR(UHCI_OFF_INTRQH);
    uhci_qh_t *cqh = (uhci_qh_t*)UHCI_PTR(UHCI_OFF_CTRLQH);
    iqh->head_link = 0x1u; iqh->element_link = 0x1u;
    cqh->head_link = 0x1u; cqh->element_link = 0x1u;

    fl[0] = UHCI_PHYS(UHCI_OFF_INTRQH) | 0x2u;
    for (int i = 1; i < 1024; i++) fl[i] = UHCI_PHYS(UHCI_OFF_CTRLQH) | 0x2u;

    _uhci_outl(io + UHCI_FRBASEADD, UHCI_PHYS(UHCI_OFF_FRAMES));
    _uhci_outw(io + UHCI_FRNUM, 0);
    _uhci_outw(io + UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF);
    uhci_delay(2);

    /* Set address */
    uhci_setup_pkt_t pkt = {0x00, 5, 1, 0, 0}; /* SET_ADDRESS = 1 */
    if (uhci_control(0, &pkt, 0, 0, false)) { kbd_addr = 1; uhci_delay(2); }

    /* Set boot protocol */
    pkt.bmRequestType = 0x21; pkt.bRequest = 11; pkt.wValue = 0;
    uhci_control(kbd_addr, &pkt, 0, 0, false);

    kbd_ep = 1; kbd_dtog = 0;
    uhci_arm_intr();
    uhci_ready = true;
    printk(T, "[UHCI] kbd ready addr=%u ep=%u ls=%d\n", kbd_addr, kbd_ep, (int)kbd_ls);
    return true;
}

static void uhci_process_report(const uhci_hid_report_t *r) {
    bool shift = !!(r->modifiers & 0x22u);
    for (int i = 0; i < 6; i++) {
        u8 kc = r->keycode[i];
        if (kc < 0x04 || kc >= 0x64) continue;
        bool was = false;
        for (int j = 0; j < 6; j++)
            if (uhci_prev_report.keycode[j] == kc) { was = true; break; }
        if (!was) {
            char c = shift ? uhci_hid_hi[kc-4] : uhci_hid_lo[kc-4];
            if (c) uhci_key_push(c);
        }
    }
    uhci_prev_report = *r;
}

void uhci_kbd_poll(void) {
    if (!uhci_ready || !intr_td) return;
    u32 cs = intr_td->ctrl_status;
    if (cs & TD_ACTIVE) return;
    if (cs & TD_NAK)    { uhci_arm_intr(); return; }
    if (cs & TD_ERRMASK){ kbd_dtog = 0; uhci_arm_intr(); return; }
    uhci_process_report((uhci_hid_report_t*)UHCI_PTR(UHCI_OFF_REPORT));
    kbd_dtog ^= 1;
    uhci_arm_intr();
}

bool uhci_kbd_ready(void) { return uhci_ready; }
