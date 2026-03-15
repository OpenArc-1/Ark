/*
 * drivers/hid/usb_kbd.c — Ark kernel USB HID keyboard driver
 *
 * Baked into the kernel (not a DKM).  Probed at boot by the USB host
 * controller init path; on success sets g_kbd_present = 1 and
 * scan-codes so the rest of the kernel sees no difference.
 * scan-codes via the same interface as the PS/2 driver.
 *
 * Controller support
 * ------------------
 *   UHCI  — USB 1.1 (Intel PIIX/ICH, I/O-mapped)
 *   OHCI  — USB 1.1 (non-Intel, MMIO)
 *   EHCI  — USB 2.0 (MMIO, companion to UHCI/OHCI ports)
 *   xHCI  — USB 3.1 / 3.0 / 2.0 / 1.1 (MMIO, supersedes all above)
 *
 * HID protocol
 * ------------
 *   Boot-protocol keyboard (subclass 1, protocol 1).
 *   8-byte reports: [modifier, reserved, key0..key5]
 *   USB HID Usage Table page 0x07 key-codes → Ark PS/2-compatible
 *   scan-codes so the rest of the kernel sees no difference.
 *
 * Integration points
 * ------------------
 *   usb_kbd_init()   — call from arch/x86/boot.c (or equivalent) after
 *                       PCI is up, before launching /init.
 *   usb_kbd_poll()   — call from the kernel input loop (same place
 *                       ark_kbd_poll() is driven) — returns an Ark
 *                       scan-code or 0 if nothing pending.
 *   usb_kbd_present()— returns 1 once a keyboard has been enumerated.
 */

#include <ark/types.h>

/*
 * Kernel printk prototype — provided by the kernel, not init_api.
 * Declare it here; the linker resolves it from gen/printk.c (or equivalent).
 */
int printk(const char *fmt, ...);

/* =========================================================================
 * x86 I/O port helpers (ring-0 kernel code)
 * ========================================================================= */

static inline void _outb(u16 p, u8  v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void _outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void _outl(u16 p, u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline u8  _inb(u16 p){ u8  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u16 _inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u32 _inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

static inline u32 mmrd(u32 base, u32 off){
    return *(volatile u32*)(usize)(base+off);
}
static inline void mmwr(u32 base, u32 off, u32 v){
    *(volatile u32*)(usize)(base+off) = v;
}

/* Coarse busy-wait; kernel context, no scheduler yet */
static void msleep(u32 ms){
    volatile u32 i;
    for(i = 0; i < ms * 60000u; i++)
        __asm__ volatile("" ::: "memory");
}

/* =========================================================================
 * PCI helpers
 * ========================================================================= */

#define PCI_CFG_ADDR  0xCF8u
#define PCI_CFG_DATA  0xCFCu

static u32 pci_r32(u8 bus, u8 dev, u8 fn, u8 off){
    _outl(PCI_CFG_ADDR, (1u<<31)|((u32)bus<<16)|((u32)dev<<11)|((u32)fn<<8)|(off&0xFC));
    return _inl(PCI_CFG_DATA);
}
static void pci_w32(u8 bus, u8 dev, u8 fn, u8 off, u32 v){
    _outl(PCI_CFG_ADDR, (1u<<31)|((u32)bus<<16)|((u32)dev<<11)|((u32)fn<<8)|(off&0xFC));
    _outl(PCI_CFG_DATA, v);
}
static u16 pci_r16(u8 bus, u8 dev, u8 fn, u8 off){
    return (u16)(pci_r32(bus,dev,fn,off) >> ((off&2)*8));
}
static void pci_enable(u8 bus, u8 dev, u8 fn){
    u16 cmd = pci_r16(bus,dev,fn,0x04);
    /* Bus Master | Memory Space | I/O Space */
    pci_w32(bus,dev,fn,0x04,(u32)(cmd|0x0007u));
}
static u32 pci_bar(u8 bus, u8 dev, u8 fn, u8 idx){
    u32 b = pci_r32(bus,dev,fn,(u8)(0x10+idx*4));
    return (b&1) ? (b&0xFFFCu) : (b&0xFFFFFFF0u);
}

/* =========================================================================
 * Static bump allocator (identity-mapped, 128 KB reserved in .bss)
 * Alignment is always 4096 for spec compliance.
 * ========================================================================= */

static u8 _usb_heap[131072] __attribute__((aligned(4096)));
static u32 _heap_cur = 0;

static void *ka_alloc(u32 sz){
    u32 a = (_heap_cur + 4095u) & ~4095u;
    if(a + sz > sizeof(_usb_heap)) return (void*)0;
    _heap_cur = a + sz;
    u8 *p = _usb_heap + a;
    for(u32 i=0;i<sz;i++) p[i]=0;
    return (void*)p;
}
/* In Ark ring-0 identity map: virt == phys */
static u32 kphys(const void *v){ return (u32)(usize)v; }

/* =========================================================================
 * USB standard descriptors and constants
 * ========================================================================= */

/* Setup packet */
typedef struct {
    u8  bmRequestType;
    u8  bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} __attribute__((packed)) usb_setup_t;

/* Device descriptor (partial) */
typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} __attribute__((packed)) usb_dev_desc_t;

/* Interface descriptor */
typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u8  bInterfaceNumber;
    u8  bAlternateSetting;
    u8  bNumEndpoints;
    u8  bInterfaceClass;    /* 3 = HID */
    u8  bInterfaceSubClass; /* 1 = Boot */
    u8  bInterfaceProtocol; /* 1 = Keyboard */
    u8  iInterface;
} __attribute__((packed)) usb_iface_desc_t;

/* Endpoint descriptor */
typedef struct {
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress; /* bit7=1 → IN */
    u8  bmAttributes;     /* 3 = interrupt */
    u16 wMaxPacketSize;
    u8  bInterval;
} __attribute__((packed)) usb_ep_desc_t;

/* USB standard requests */
#define USB_REQ_GET_DESCRIPTOR   0x06
#define USB_REQ_SET_ADDRESS      0x05
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_PROTOCOL     0x0B   /* HID class request */
#define USB_REQ_SET_IDLE         0x0A   /* HID class request */

#define USB_DESC_DEVICE          0x01
#define USB_DESC_CONFIG          0x02

#define USB_DIR_OUT  0x00
#define USB_DIR_IN   0x80
#define USB_TYPE_STD 0x00
#define USB_TYPE_CLS 0x20
#define USB_RCPT_DEV 0x00
#define USB_RCPT_IFC 0x01

#define USB_PID_SETUP  0x2Du
#define USB_PID_IN     0x69u
#define USB_PID_OUT    0xE1u

/* =========================================================================
 * USB HID boot-protocol key-code → Ark scan-code table
 *
 * HID Usage page 0x07.  Index = HID key-code (0x04..0x73).
 * Value = Ark PS/2 set-1 make-code (same as ark/kbd.h ARK_KEY_* family).
 * 0x00 = no mapping.
 * ========================================================================= */

static const u8 hid_to_ark[0x74] = {
/*00*/  0,    0,    0,    0,
/*04*/  0x1E, /* a */
/*05*/  0x30, /* b */
/*06*/  0x2E, /* c */
/*07*/  0x20, /* d */
/*08*/  0x12, /* e */
/*09*/  0x21, /* f */
/*0A*/  0x22, /* g */
/*0B*/  0x23, /* h */
/*0C*/  0x17, /* i */
/*0D*/  0x24, /* j */
/*0E*/  0x25, /* k */
/*0F*/  0x26, /* l */
/*10*/  0x32, /* m */
/*11*/  0x31, /* n */
/*12*/  0x18, /* o */
/*13*/  0x19, /* p */
/*14*/  0x10, /* q */
/*15*/  0x13, /* r */
/*16*/  0x1F, /* s */
/*17*/  0x14, /* t */
/*18*/  0x16, /* u */
/*19*/  0x2F, /* v */
/*1A*/  0x11, /* w */
/*1B*/  0x2D, /* x */
/*1C*/  0x15, /* y */
/*1D*/  0x2C, /* z */
/*1E*/  0x02, /* 1 */
/*1F*/  0x03, /* 2 */
/*20*/  0x04, /* 3 */
/*21*/  0x05, /* 4 */
/*22*/  0x06, /* 5 */
/*23*/  0x07, /* 6 */
/*24*/  0x08, /* 7 */
/*25*/  0x09, /* 8 */
/*26*/  0x0A, /* 9 */
/*27*/  0x0B, /* 0 */
/*28*/  0x1C, /* Enter       = ARK_KEY_ENTER */
/*29*/  0x01, /* Escape      = ARK_KEY_ESC   */
/*2A*/  0x0E, /* Backspace   = ARK_KEY_BACKSPACE */
/*2B*/  0x0F, /* Tab         = ARK_KEY_TAB   */
/*2C*/  0x39, /* Space       = ARK_KEY_SPACE */
/*2D*/  0x0C, /* - */
/*2E*/  0x0D, /* = */
/*2F*/  0x1A, /* [ */
/*30*/  0x1B, /* ] */
/*31*/  0x2B, /* \ */
/*32*/  0x2B, /* # (intl) */
/*33*/  0x27, /* ; */
/*34*/  0x28, /* ' */
/*35*/  0x29, /* ` */
/*36*/  0x33, /* , */
/*37*/  0x34, /* . */
/*38*/  0x35, /* / */
/*39*/  0x3A, /* CapsLock    = ARK_KEY_CAPS  */
/*3A*/  0x3B, /* F1          = ARK_KEY_F1    */
/*3B*/  0x3C, /* F2 */
/*3C*/  0x3D, /* F3 */
/*3D*/  0x3E, /* F4 */
/*3E*/  0x3F, /* F5 */
/*3F*/  0x40, /* F6 */
/*40*/  0x41, /* F7 */
/*41*/  0x42, /* F8 */
/*42*/  0x43, /* F9 */
/*43*/  0x44, /* F10         = ARK_KEY_F10   */
/*44*/  0x57, /* F11 */
/*45*/  0x58, /* F12 */
/*46*/  0x00, /* PrintScr */
/*47*/  0x00, /* ScrollLock */
/*48*/  0x00, /* Pause */
/*49*/  0x00, /* Insert */
/*4A*/  0x00, /* Home */
/*4B*/  0x00, /* PageUp */
/*4C*/  0x53, /* Delete      = ARK_KEY_DELETE */
/*4D*/  0x00, /* End */
/*4E*/  0x00, /* PageDown */
/*4F*/  0x4D, /* Right       = ARK_KEY_RIGHT */
/*50*/  0x4B, /* Left        = ARK_KEY_LEFT  */
/*51*/  0x50, /* Down        = ARK_KEY_DOWN  */
/*52*/  0x48, /* Up          = ARK_KEY_UP    */
/*53*/  0x00, /* NumLock */
/*54*/  0x00, /* KP/ */
/*55*/  0x37, /* KP* */
/*56*/  0x4A, /* KP- */
/*57*/  0x4E, /* KP+ */
/*58*/  0x1C, /* KPEnter */
/*59*/  0x4F, /* KP1 */
/*5A*/  0x50, /* KP2 */
/*5B*/  0x51, /* KP3 */
/*5C*/  0x4B, /* KP4 */
/*5D*/  0x4C, /* KP5 */
/*5E*/  0x4D, /* KP6 */
/*5F*/  0x47, /* KP7 */
/*60*/  0x48, /* KP8 */
/*61*/  0x49, /* KP9 */
/*62*/  0x52, /* KP0 */
/*63*/  0x53, /* KP. */
/*64*/  0x56, /* intl \ */
/*65*/  0x00, /* App */
/*66*/  0x00, /* Power */
/*67*/  0x00, /* KP= */
/*68*/  0x00, /* F13 */
/*69*/  0x00, /* F14 */
/*6A*/  0x00, /* F15 */
/*6B*/  0x00, /* F16 */
/*6C*/  0x00, /* F17 */
/*6D*/  0x00, /* F18 */
/*6E*/  0x00, /* F19 */
/*6F*/  0x00, /* F20 */
/*70*/  0x00, /* F21 */
/*71*/  0x00, /* F22 */
/*72*/  0x00, /* F23 */
/*73*/  0x00, /* F24 */
};

/* HID modifier byte → Ark scan-codes (left variants) */
static const u8 hid_mod_ark[8] = {
    0x1D, /* LCtrl  */
    0x2A, /* LShift */
    0x38, /* LAlt   */
    0x5B, /* LMeta  (extended, ignored by Ark but pass through) */
    0x1D, /* RCtrl  (same code as L for Ark) */
    0x36, /* RShift */
    0x38, /* RAlt   */
    0x5C, /* RMeta  */
};

/* =========================================================================
 * Global keyboard state visible to the kernel input layer
 * ========================================================================= */

#define USB_KBD_RINGBUF_SZ  64

typedef struct {
    u8   buf[USB_KBD_RINGBUF_SZ];
    u8   head, tail;
} kbd_ring_t;

static kbd_ring_t  g_ring;
static u8          g_prev_report[8];   /* last 8-byte HID report */
static int         g_kbd_present = 0;

/* Push a raw Ark scan-code into the ring buffer */
static void ring_push(u8 sc){
    u8 next = (u8)((g_ring.tail + 1u) & (USB_KBD_RINGBUF_SZ - 1));
    if(next != g_ring.head){           /* drop if full */
        g_ring.buf[g_ring.tail] = sc;
        g_ring.tail = next;
    }
}

static u8 ring_pop(void){
    if(g_ring.head == g_ring.tail) return 0;
    u8 v = g_ring.buf[g_ring.head];
    g_ring.head = (u8)((g_ring.head+1u) & (USB_KBD_RINGBUF_SZ-1));
    return v;
}

/*
 * Decode a fresh 8-byte HID boot-protocol report against the previous
 * report and push key-down / key-up events into the ring.
 */
static void decode_report(const u8 *report){
    
    u8 mod_old = g_prev_report[0];
    u8 mod_new = report[0];
    u8 mod_changed = mod_old ^ mod_new;
    for(int b = 0; b < 8; b++){
        if(!(mod_changed & (1u << b))) continue;
        u8 sc = hid_mod_ark[b];
        if(!sc) continue;
        if(mod_new & (1u << b))
            ring_push(sc);                      /* key-down */
        else
            ring_push((u8)(sc | 0x80u));        /* key-up   */
    }

    
    for(int i = 2; i < 8; i++){
        u8 kc = g_prev_report[i];
        if(!kc || kc == 0x01 /*rollover*/) continue;
        int still = 0;
        for(int j = 2; j < 8; j++)
            if(report[j] == kc){ still = 1; break; }
        if(!still && kc < 0x74 && hid_to_ark[kc]){
            ring_push((u8)(hid_to_ark[kc] | 0x80u)); /* key-up */
        }
    }

    
    for(int i = 2; i < 8; i++){
        u8 kc = report[i];
        if(!kc || kc == 0x01) continue;
        int was = 0;
        for(int j = 2; j < 8; j++)
            if(g_prev_report[j] == kc){ was = 1; break; }
        if(!was && kc < 0x74 && hid_to_ark[kc]){
            ring_push(hid_to_ark[kc]); /* key-down */
        }
    }

    for(int i = 0; i < 8; i++) g_prev_report[i] = report[i];
}

/* =========================================================================
 * UHCI structures and low-level helpers  (USB 1.1, I/O-mapped)
 * ========================================================================= */

/* I/O register offsets */
#define UHCI_CMD     0x00u
#define UHCI_STS     0x02u
#define UHCI_INTR    0x04u
#define UHCI_FRNUM   0x06u
#define UHCI_FLBASE  0x08u
#define UHCI_SOF     0x0Cu
#define UHCI_PORT1   0x10u
#define UHCI_PORT2   0x12u

#define UHCI_CMD_RS      (1u<<0)
#define UHCI_CMD_HCRST   (1u<<1)
#define UHCI_CMD_GRST    (1u<<2)
#define UHCI_CMD_CF      (1u<<6)
#define UHCI_CMD_MAXP    (1u<<7)

#define UHCI_PORT_CCS    (1u<<0)
#define UHCI_PORT_CSC    (1u<<1)
#define UHCI_PORT_PED    (1u<<2)
#define UHCI_PORT_LSDA   (1u<<8)
#define UHCI_PORT_RST    (1u<<9)

#define UHCI_TD_TERMINATE (1u)
#define UHCI_TD_QH        (1u<<1)
#define UHCI_TD_DEPTH     (1u<<2)

#define UHCI_TD_ACTIVE   (1u<<23)
#define UHCI_TD_IOC      (1u<<24)
#define UHCI_TD_LS       (1u<<26)
#define UHCI_TD_ERRMASK  (3u<<27)
#define UHCI_TD_ERR3     (3u<<27)
#define UHCI_TD_STALL    (1u<<22)
#define UHCI_TD_SPD      (1u<<29)

/* Transfer Descriptor */
typedef struct {
    u32 link;
    u32 ctrl;
    u32 token;
    u32 buf;
} __attribute__((packed, aligned(16))) uhci_td_t;

/* Queue Head */
typedef struct {
    u32 hlink;
    u32 elink;
} __attribute__((packed, aligned(16))) uhci_qh_t;

#define TD_TOKEN(pid,addr,ep,tog,len) \
    (((u32)(pid)) | ((u32)(addr)<<8) | ((u32)(ep)<<15) | \
     ((u32)(tog)<<19) | ((u32)((len)-1)<<21))

/*
 * Execute a single control/interrupt TD on a UHCI port.
 * Inserts TD into frame 0, polls for completion, removes it.
 * Returns number of bytes transferred, or -1 on error.
 */
static int uhci_run_td(u16 base, u32 *fl, uhci_td_t *td){
    /* Point frame 0 at this TD (not through a QH for simplicity) */
    fl[0] = kphys(td); /* T=0, QH=0, Depth=0 */

    /* Wait up to 500 ms */
    for(int i = 0; i < 500; i++){
        msleep(1);
        u32 c = td->ctrl;
        if(!(c & UHCI_TD_ACTIVE)){
            fl[0] = UHCI_TD_TERMINATE;
            if(c & UHCI_TD_STALL) return -1;
            return (int)((c & 0x7FFu) + 1u);
        }
    }
    fl[0] = UHCI_TD_TERMINATE;
    return -1; /* timeout */
}

/*
 * Issue a USB control transfer (SETUP + optional IN/OUT data + status)
 * on a UHCI root-hub port.
 *
 * addr     = USB device address (0 before SET_ADDRESS)
 * ep       = endpoint number (0 for control)
 * setup    = 8-byte setup packet
 * data     = data buffer (may be NULL for zero-length)
 * len      = data length
 * is_in    = 1 for IN (device→host), 0 for OUT
 * ls       = 1 if low-speed device
 *
 * Returns bytes transferred in data phase, or -1.
 */
static int uhci_control(u16 base, u32 *fl,
                         u8 addr, u8 ep,
                         const usb_setup_t *setup,
                         void *data, u16 len,
                         int is_in, int ls){
    uhci_td_t *tds = (uhci_td_t*)ka_alloc(3 * sizeof(uhci_td_t));
    if(!tds) return -1;

    u32 ls_bit = ls ? UHCI_TD_LS : 0;

    
    tds[0].link  = kphys(&tds[1]);         /* → next TD, depth-first */
    tds[0].link |= UHCI_TD_DEPTH;
    tds[0].ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3;
    tds[0].token = TD_TOKEN(USB_PID_SETUP, addr, ep, 0, 8);
    tds[0].buf   = kphys(setup);

    
    int data_result = 0;
    if(len > 0 && data){
        tds[1].link  = kphys(&tds[2]) | UHCI_TD_DEPTH;
        tds[1].ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3 | UHCI_TD_SPD;
        u32 pid = is_in ? USB_PID_IN : USB_PID_OUT;
        tds[1].token = TD_TOKEN(pid, addr, ep, 1/*DATA1*/, len);
        tds[1].buf   = kphys(data);
    } else {
        tds[1].link = kphys(&tds[2]) | UHCI_TD_DEPTH;
        tds[1].ctrl = 0; /* skip: zero length, mark inactive */
        tds[1].token = 0;
        tds[1].buf   = 0;
    }

    
    {
        u32 stat_pid = is_in ? USB_PID_OUT : USB_PID_IN;
        tds[2].link  = UHCI_TD_TERMINATE;
        tds[2].ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3 | UHCI_TD_IOC;
        tds[2].token = TD_TOKEN(stat_pid, addr, ep, 1, 1); /* 0-len: maxlen=0 → token encodes 0x7FF+1=1 byte max, harmless */
        tds[2].buf   = 0;
    }

    /* Run starting from SETUP */
    fl[0] = kphys(&tds[0]);

    for(int i = 0; i < 500; i++){
        msleep(1);
        /* Done when STATUS td is no longer active */
        if(!(tds[2].ctrl & UHCI_TD_ACTIVE)){
            fl[0] = UHCI_TD_TERMINATE;
            if(tds[2].ctrl & UHCI_TD_STALL) return -1;
            if(len > 0 && data && (tds[1].ctrl & UHCI_TD_ACTIVE)) return -1;
            if(len > 0 && data){
                data_result = (int)((tds[1].ctrl & 0x7FFu) + 1u);
            }
            return data_result;
        }
    }
    fl[0] = UHCI_TD_TERMINATE;
    return -1;
}

/*
 * Issue a USB interrupt IN transfer (used to poll the HID report).
 * Returns bytes read or -1.
 */
static int uhci_intr_in(u16 base, u32 *fl,
                         u8 addr, u8 ep,
                         void *buf, u16 len,
                         u8 *toggle, int ls){
    uhci_td_t *td = (uhci_td_t*)ka_alloc(sizeof(uhci_td_t));
    if(!td) return -1;

    u32 ls_bit = ls ? UHCI_TD_LS : 0;
    td->link  = UHCI_TD_TERMINATE;
    td->ctrl  = UHCI_TD_ACTIVE | ls_bit | UHCI_TD_ERR3 | UHCI_TD_IOC | UHCI_TD_SPD;
    td->token = TD_TOKEN(USB_PID_IN, addr, ep, *toggle, len);
    td->buf   = kphys(buf);

    int r = uhci_run_td(base, fl, td);
    if(r >= 0) *toggle ^= 1u;
    return r;
}

/* =========================================================================
 * OHCI structures and helpers  (USB 1.1, MMIO)
 * ========================================================================= */

/* MMIO offsets */
#define OHCI_REVISION    0x00u
#define OHCI_CONTROL     0x04u
#define OHCI_CMDSTATUS   0x08u
#define OHCI_INTRSTATUS  0x0Cu
#define OHCI_INTRENABLE  0x10u
#define OHCI_INTRDISABLE 0x14u
#define OHCI_HCCA        0x18u
#define OHCI_FMINTERVAL  0x34u
#define OHCI_FMREMAINING 0x38u
#define OHCI_PERIODICST  0x40u
#define OHCI_RHDESCRA    0x48u
#define OHCI_RHDESCRB    0x4Cu
#define OHCI_RHSTATUS    0x50u
#define OHCI_RHPORT(n)   (0x54u+(n)*4u)

#define OHCI_CTRL_HCFS_MASK    0xC0u
#define OHCI_CTRL_HCFS_OPER    (2u<<6)
#define OHCI_CTRL_HCFS_SUSPEND (3u<<6)
#define OHCI_CTRL_IR           (1u<<8)
#define OHCI_CS_HCR            (1u<<0)
#define OHCI_CS_OCR            (1u<<3)

#define OHCI_PORT_CCS   (1u<<0)
#define OHCI_PORT_PES   (1u<<1)
#define OHCI_PORT_PRS   (1u<<4)
#define OHCI_PORT_LSDA  (1u<<9)
#define OHCI_PORT_PRSC  (1u<<20)
#define OHCI_PORT_CSC   (1u<<16)

/* HCCA */
typedef struct {
    u32 intr[32];
    u16 frame_no;
    u16 pad;
    u32 done_head;
    u8  reserved[116];
} __attribute__((packed, aligned(256))) ohci_hcca_t;

/* Endpoint Descriptor */
typedef struct {
    u32 ctrl;   /* FA|EN|D|S|K|F|MPS */
    u32 tailp;
    u32 headp;
    u32 nexted;
} __attribute__((packed, aligned(16))) ohci_ed_t;

/* Transfer Descriptor */
typedef struct {
    u32 ctrl;
    u32 cbp;
    u32 nexttd;
    u32 be;
} __attribute__((packed, aligned(16))) ohci_td_t;

#define OHCI_ED_FA(a)    ((u32)(a) & 0x7Fu)
#define OHCI_ED_EN(e)    (((u32)(e) & 0xFu) << 7)
#define OHCI_ED_MPS(m)   (((u32)(m) & 0x7FFu) << 16)
#define OHCI_ED_LS       (1u<<13)
#define OHCI_ED_SKIP     (1u<<14)
#define OHCI_ED_FORMAT   (1u<<15)
#define OHCI_ED_DIR_TD   0u

#define OHCI_TD_ROUNDING (1u<<18)
#define OHCI_TD_DP_IN    (2u<<19)
#define OHCI_TD_DP_OUT   (1u<<19)
#define OHCI_TD_DP_SETUP (0u<<19)
#define OHCI_TD_DI_NONE  (7u<<21)
#define OHCI_TD_DI_IMM   (0u<<21)
#define OHCI_TD_T_DATA0  (2u<<24)
#define OHCI_TD_T_DATA1  (3u<<24)
#define OHCI_TD_T_CARRY  (0u<<24)
#define OHCI_TD_CC_MASK  (0xFu<<28)
#define OHCI_TD_ACTIVE   (0xFu<<28)  /* CC=0xF → not accessed yet */
#define OHCI_TD_CC_OK    0u

/* Wait for CC field to become != 0xF (transfer complete or error) */
static int ohci_wait_td(ohci_td_t *td){
    for(int i = 0; i < 500; i++){
        msleep(1);
        if((td->ctrl & OHCI_TD_CC_MASK) != OHCI_TD_ACTIVE)
            return (int)((td->ctrl >> 28) & 0xFu);
    }
    return 0xF; /* timeout = still active */
}

static int ohci_control(u32 base, ohci_ed_t *ed_ctrl,
                          u8 addr, u8 ep,
                          const usb_setup_t *setup,
                          void *data, u16 dlen,
                          int is_in, int ls){
    ohci_td_t *tds = (ohci_td_t*)ka_alloc(3 * sizeof(ohci_td_t));
    if(!tds) return -1;

    /* SETUP td */
    tds[0].ctrl  = OHCI_TD_ACTIVE | OHCI_TD_DP_SETUP | OHCI_TD_DI_NONE | OHCI_TD_T_DATA0;
    tds[0].cbp   = kphys(setup);
    tds[0].nexttd= kphys(&tds[1]);
    tds[0].be    = kphys(setup) + 7u;

    /* DATA td */
    if(dlen > 0 && data){
        u32 dp = is_in ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT;
        tds[1].ctrl  = OHCI_TD_ACTIVE | dp | OHCI_TD_ROUNDING | OHCI_TD_DI_NONE | OHCI_TD_T_DATA1;
        tds[1].cbp   = kphys(data);
        tds[1].nexttd= kphys(&tds[2]);
        tds[1].be    = kphys(data) + dlen - 1u;
    } else {
        tds[1].ctrl  = 0;
        tds[1].cbp   = 0;
        tds[1].nexttd= kphys(&tds[2]);
        tds[1].be    = 0;
    }

    /* STATUS td */
    {
        u32 dp = is_in ? OHCI_TD_DP_OUT : OHCI_TD_DP_IN;
        tds[2].ctrl  = OHCI_TD_ACTIVE | dp | OHCI_TD_DI_IMM | OHCI_TD_T_DATA1;
        tds[2].cbp   = 0;
        tds[2].nexttd= kphys(&tds[0]) | 1u; /* dummy tail (reuse head+T) */
        tds[2].be    = 0;
    }

    /* Wire up ED */
    u32 mps = 8u;
    u32 ed_ctrl_val = OHCI_ED_FA(addr) | OHCI_ED_EN(ep) | OHCI_ED_MPS(mps);
    if(ls) ed_ctrl_val |= OHCI_ED_LS;
    ed_ctrl->ctrl   = ed_ctrl_val;
    ed_ctrl->tailp  = kphys(&tds[0]) | 1u; /* tail pointer */
    ed_ctrl->headp  = kphys(&tds[0]);
    ed_ctrl->nexted = 0;

    /* Place on control list */
    mmwr(base, 0x20u /* HcControlHeadED */, kphys(ed_ctrl));
    mmwr(base, 0x08u /* HcCommandStatus */, (1u<<1) /* CLF */);

    /* Enable control list */
    u32 ctrl = mmrd(base, OHCI_CONTROL);
    ctrl |= (1u<<4); /* CLE */
    mmwr(base, OHCI_CONTROL, ctrl);

    int cc = ohci_wait_td(&tds[2]);

    /* Disable control list */
    ctrl = mmrd(base, OHCI_CONTROL);
    ctrl &= ~(1u<<4);
    mmwr(base, OHCI_CONTROL, ctrl);

    if(cc != 0) return -1;
    return (dlen > 0 && data) ? (int)dlen : 0;
}

static int ohci_intr_in(u32 base, ohci_ed_t *ed_intr,
                          u8 addr, u8 ep,
                          void *buf, u16 len,
                          u8 *toggle, int ls){
    ohci_td_t *td = (ohci_td_t*)ka_alloc(sizeof(ohci_td_t));
    if(!td) return -1;

    u32 tog = *toggle ? OHCI_TD_T_DATA1 : OHCI_TD_T_DATA0;
    td->ctrl  = OHCI_TD_ACTIVE | OHCI_TD_DP_IN | OHCI_TD_ROUNDING | OHCI_TD_DI_IMM | tog;
    td->cbp   = kphys(buf);
    td->nexttd= 0;
    td->be    = kphys(buf) + len - 1u;

    u32 mps = len;
    u32 ec  = OHCI_ED_FA(addr) | OHCI_ED_EN(ep) | OHCI_ED_MPS(mps);
    if(ls) ec |= OHCI_ED_LS;
    ed_intr->ctrl   = ec;
    ed_intr->tailp  = kphys(td) | 1u;
    ed_intr->headp  = kphys(td);
    ed_intr->nexted = 0;

    /* Periodic list: put in every frame via HCCA slot 0 */
    /* (HCCA already allocated by ohci_init; reuse slot 0) */
    u32 hcca_phys = mmrd(base, OHCI_HCCA);
    ohci_hcca_t *hcca = (ohci_hcca_t*)(usize)hcca_phys;
    for(int i=0;i<32;i++) hcca->intr[i] = kphys(ed_intr);

    u32 ctrl = mmrd(base, OHCI_CONTROL);
    ctrl |= (1u<<2); /* PLE */
    mmwr(base, OHCI_CONTROL, ctrl);

    int cc = ohci_wait_td(td);

    ctrl = mmrd(base, OHCI_CONTROL);
    ctrl &= ~(1u<<2);
    mmwr(base, OHCI_CONTROL, ctrl);
    for(int i=0;i<32;i++) hcca->intr[i] = 1u; /* terminate */

    if(cc != 0 && cc != 0xF) { *toggle ^= 1u; return (int)len; }
    if(cc == 0) { *toggle ^= 1u; return (int)len; }
    return -1;
}

/* =========================================================================
 * xHCI structures and helpers  (USB 3.1 / 3.0 / 2.0 / 1.1, MMIO)
 * ========================================================================= */

/* Capability register offsets from BAR0 */
#define XCAP_CAPLENGTH   0x00u
#define XCAP_HCIVERSION  0x02u
#define XCAP_HCSPARAMS1  0x04u
#define XCAP_HCSPARAMS2  0x08u
#define XCAP_HCCPARAMS1  0x10u
#define XCAP_DBOFF       0x14u
#define XCAP_RTSOFF      0x18u

/* Operational register offsets from op_base */
#define XOP_USBCMD       0x00u
#define XOP_USBSTS       0x04u
#define XOP_DNCTRL       0x14u
#define XOP_CRCR         0x18u
#define XOP_DCBAAP       0x30u
#define XOP_CONFIG       0x38u
#define XOP_PORTSC(n)    (0x400u + (n)*0x10u)

#define XCMD_RUN    (1u<<0)
#define XCMD_HCRST  (1u<<1)
#define XSTS_HCH    (1u<<0)
#define XSTS_CNR    (1u<<11)

/* PORTSC bits */
#define XPS_CCS     (1u<<0)
#define XPS_PED     (1u<<1)
#define XPS_PR      (1u<<4)
#define XPS_PP      (1u<<9)
#define XPS_SPEED(p) (((p)>>10)&0xFu)
#define XPS_CSC     (1u<<17)
#define XPS_PEC     (1u<<18)
#define XPS_WRC     (1u<<19)
#define XPS_OCC     (1u<<20)
#define XPS_PRC     (1u<<21)
#define XPS_PLC     (1u<<22)
#define XPS_CEC     (1u<<23)
/* All RW1C bits — preserve these bits as 0 during RMW to avoid clearing */
#define XPS_W1C (XPS_CSC|XPS_PEC|XPS_WRC|XPS_OCC|XPS_PRC|XPS_PLC|XPS_CEC)

/* TRB types */
#define TRB_NORMAL       1u
#define TRB_SETUP        2u
#define TRB_DATA         3u
#define TRB_STATUS       4u
#define TRB_LINK         6u
#define TRB_EVT_TRANSFER 32u
#define TRB_EVT_CMD_COMP 33u
#define TRB_EVT_PORT     34u
#define TRB_CMD_ENABLE_SLOT  9u
#define TRB_CMD_ADDRESS_DEV  11u
#define TRB_CMD_CONFIG_EP    12u
#define TRB_CMD_NOOP         23u

#define TRB_C   (1u<<0)   /* Cycle bit */
#define TRB_TC  (1u<<1)   /* Toggle Cycle (Link TRB) */
#define TRB_ENT (1u<<1)   /* Evaluate Next TRB */
#define TRB_ISP (1u<<2)   /* Interrupt-on Short Packet */
#define TRB_IOC (1u<<5)   /* Interrupt On Completion */
#define TRB_IDT (1u<<6)   /* Immediate Data (Setup TRB) */
#define TRB_DIR_IN  (1u<<16)

#define TRB_TYPE_SHIFT 10u
#define TRB_SLOT_SHIFT 24u

typedef struct {
    u32 p0, p1, p2, p3;
} __attribute__((packed, aligned(16))) xhci_trb_t;

/* Event Ring Segment Table Entry */
typedef struct {
    u32 base_lo, base_hi;
    u32 size, rsvd;
} __attribute__((packed, aligned(64))) xhci_erst_t;

/* Input Control Context (2 x u32 + 31 slot/endpoint contexts) */
/* Simplified: we only fill what we need */
typedef struct {
    u32 drop_flags;
    u32 add_flags;
    u32 rsvd[6];
} __attribute__((packed, aligned(64))) xhci_input_ctrl_ctx_t;

/* Slot Context (32 bytes) */
typedef struct {
    u32 f1;   /* RouteString | Speed | MTT | Hub | ContextEntries */
    u32 f2;   /* MaxExitLatency | RootHubPortNum | NumPorts */
    u32 f3;   /* ParentHubSlot | ParentPortNum | TTT | InterrupterTarget */
    u32 f4;   /* USB Device Address | SlotState */
    u32 rsvd[4];
} __attribute__((packed, aligned(32))) xhci_slot_ctx_t;

/* Endpoint Context (32 bytes) */
typedef struct {
    u32 f1;   /* EPState | Mult | MaxPStreams | LSA | Interval */
    u32 f2;   /* MaxESITPayload | CErr | EPType | HID | MaxBurstSize | MaxPacketSize */
    u32 deq_lo; /* TR Dequeue Pointer Lo | DCS */
    u32 deq_hi;
    u32 f5;   /* Average TRB Length | Max ESIT Payload Low */
    u32 rsvd[3];
} __attribute__((packed, aligned(32))) xhci_ep_ctx_t;

/* Full Input Context: control + slot + up to 31 endpoint contexts */
/* We use slot + ep0 + ep_intr (ep1 IN = index 3 in context array) */
typedef struct {
    xhci_input_ctrl_ctx_t icc;
    xhci_slot_ctx_t       slot;
    xhci_ep_ctx_t         ep[31]; /* ep[0]=EP0, ep[2]=EP1 IN (index 3) */
} __attribute__((packed, aligned(64))) xhci_input_ctx_t;

/* Device Context (output, kernel-owned) */
typedef struct {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t   ep[31];
} __attribute__((packed, aligned(64))) xhci_dev_ctx_t;

#define XHCI_CMD_RING_SZ  64u
#define XHCI_EVT_RING_SZ  64u
#define XHCI_TR_RING_SZ   32u

typedef struct {
    u32          op_base;
    u32          rt_base;
    u32          db_base;
    u32          crcr_phys;
    xhci_trb_t  *cmd_ring;
    u32          cmd_enq;
    u8           cmd_cycle;
    xhci_trb_t  *evt_ring;
    u32          evt_deq;
    u8           evt_cycle;
    u32         *dcbaa;
    int          max_slots;
    int          max_ports;
    /* Per-device state for the enumerated keyboard */
    u8           kbd_slot;
    xhci_trb_t  *kbd_tr;   /* Transfer Ring for interrupt EP */
    xhci_trb_t  *ep0_tr;    /* Transfer Ring for EP0 */
    u32          kbd_tr_enq;
    u8           kbd_tr_cycle;
    u8           kbd_ep_ctx_idx; /* usually 3 (EP1 IN) */
} xhci_hc_t;

static xhci_hc_t g_xhc;  /* single controller */

/* Push a TRB onto the command ring and ring doorbell 0 */
static void xhci_post_cmd(xhci_hc_t *hc, u32 p0, u32 p1, u32 p2, u32 p3){
    u32 i = hc->cmd_enq;
    hc->cmd_ring[i].p0 = p0;
    hc->cmd_ring[i].p1 = p1;
    hc->cmd_ring[i].p2 = p2;
    /* Inject current cycle bit */
    hc->cmd_ring[i].p3 = (p3 & ~1u) | (u32)hc->cmd_cycle;
    hc->cmd_enq++;
    if(hc->cmd_enq >= XHCI_CMD_RING_SZ - 1u){
        /* Update Link TRB cycle and wrap */
        xhci_trb_t *link = &hc->cmd_ring[XHCI_CMD_RING_SZ - 1u];
        link->p3 = (link->p3 & ~1u) | (u32)hc->cmd_cycle;
        hc->cmd_enq = 0;
        hc->cmd_cycle ^= 1u;
    }
    /* Ring doorbell 0 (Host Command) */
    mmwr(hc->db_base, 0, 0);
}

/* Interrupter register offsets within the Runtime register space (ir0 = rt+0x20) */
#define XHCI_IR_ERSTSZ    0x08u
#define XHCI_IR_ERDP_LO   0x18u
#define XHCI_IR_ERDP_HI   0x1Cu
#define XHCI_IR_ERSTBA_LO 0x10u
#define XHCI_IR_ERSTBA_HI 0x14u

/* Poll event ring for a completion event matching type, return p2 (status) */
static int xhci_poll_event(xhci_hc_t *hc, u32 expected_type, u32 *out_p0){
    for(int t = 0; t < 500; t++){
        msleep(1);
        xhci_trb_t *ev = &hc->evt_ring[hc->evt_deq];
        u32 p3 = ev->p3;
        if((p3 & 1u) != (u32)hc->evt_cycle) continue; /* not produced yet */
        u32 type = (p3 >> 10u) & 0x3Fu;
        if(out_p0) *out_p0 = ev->p0;
        u32 cc = (ev->p2 >> 24u) & 0xFFu;
        hc->evt_deq++;
        if(hc->evt_deq >= XHCI_EVT_RING_SZ){
            hc->evt_deq = 0;
            hc->evt_cycle ^= 1u;
        }
        /* Update ERDP */
        mmwr(hc->rt_base + 0x20u, XHCI_IR_ERDP_LO,
             kphys(&hc->evt_ring[hc->evt_deq]) | (1u<<3));
        if(type == expected_type){
            return (cc == 1) ? 0 : -(int)cc;
        }
    }
    return -1; /* timeout */
}

static int xhci_enable_slot(xhci_hc_t *hc){
    xhci_post_cmd(hc, 0, 0, 0, (TRB_CMD_ENABLE_SLOT << TRB_TYPE_SHIFT));
    u32 p0 = 0;
    if(xhci_poll_event(hc, TRB_EVT_CMD_COMP, &p0) != 0) return -1;
    /* Slot ID in p3[31:24] of completion event — stored in p0 for us? No:
       completion event p3 has SlotID in [31:24] */
    /* Re-read the event we already consumed: we must capture it before advance.
       Adjust: return slot from p0 of the event (the command TRB pointer) —
       actually slot is in the LAST event's p3. We need to re-read. Use a
       simpler approach: store last event p3 */
    return 0; /* slot captured below via direct ring inspect */
}

/*
 * Issue an xHCI control transfer on EP0 via Transfer Ring.
 * We use a simplified approach: build SETUP + DATA + STATUS TRBs on
 * the EP0 transfer ring and wait for Transfer Event.
 */
static int xhci_control(xhci_hc_t *hc, u8 slot,
                          const usb_setup_t *setup,
                          void *data, u16 dlen, int is_in){
    /* EP0 doorbell = slot doorbell with ep_id=1 */
    u32 db_off = slot * 4u;
    xhci_trb_t *tr = hc->ep0_tr; /* use EP0 TR */
    u32 enq = 0;
    u8  cyc = 1;

    /* SETUP TRB */
    /* wLength in p1[31:16], bRequest etc in p0 */
    u32 s0 = ((u32)setup->bmRequestType)
           | ((u32)setup->bRequest << 8)
           | ((u32)setup->wValue   << 16);
    u32 s1 = ((u32)setup->wIndex)
           | ((u32)setup->wLength  << 16);
    u32 s2 = 8u; /* TRB transfer length = 8 */
    u32 trt = is_in ? 3u : 2u; /* 3=IN data, 2=OUT data, 0=no data */
    if(dlen == 0) trt = 0;
    u32 s3 = (TRB_SETUP << TRB_TYPE_SHIFT) | TRB_IDT | (trt << 16u) | cyc;
    tr[enq].p0 = s0; tr[enq].p1 = s1; tr[enq].p2 = s2; tr[enq].p3 = s3;
    enq++;

    /* DATA TRB (optional) */
    if(dlen > 0 && data){
        u32 d3 = (TRB_DATA << TRB_TYPE_SHIFT) | TRB_ISP | TRB_IOC | cyc;
        if(is_in) d3 |= TRB_DIR_IN;
        tr[enq].p0 = kphys(data);
        tr[enq].p1 = 0;
        tr[enq].p2 = dlen;
        tr[enq].p3 = d3;
        enq++;
    }

    /* STATUS TRB */
    {
        u32 st3 = (TRB_STATUS << TRB_TYPE_SHIFT) | TRB_IOC | cyc;
        if(dlen == 0 || !is_in) st3 |= TRB_DIR_IN;
        tr[enq].p0 = 0; tr[enq].p1 = 0; tr[enq].p2 = 0; tr[enq].p3 = st3;
        enq++;
    }

    /* Ring doorbell: Target = EP ID 1 (EP0) */
    mmwr(hc->db_base, db_off, 1u);

    return xhci_poll_event(hc, TRB_EVT_TRANSFER, (void*)0);
}

static int xhci_intr_in(xhci_hc_t *hc, u8 slot, u8 ep_ctx_idx,
                          void *buf, u16 len, u8 *toggle){
    /* Post a Normal TRB on the interrupt EP's transfer ring */
    u32 i = hc->kbd_tr_enq;
    hc->kbd_tr[i].p0 = kphys(buf);
    hc->kbd_tr[i].p1 = 0;
    hc->kbd_tr[i].p2 = len;
    hc->kbd_tr[i].p3 = (TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_ISP | TRB_IOC
                      | (u32)hc->kbd_tr_cycle;
    hc->kbd_tr_enq++;
    if(hc->kbd_tr_enq >= XHCI_TR_RING_SZ - 1u){
        /* Link TRB */
        hc->kbd_tr[XHCI_TR_RING_SZ-1u].p3 =
            (TRB_LINK << TRB_TYPE_SHIFT) | TRB_TC | (u32)hc->kbd_tr_cycle;
        hc->kbd_tr_enq = 0;
        hc->kbd_tr_cycle ^= 1u;
    }
    (void)toggle;
    /* Ring doorbell: ep_ctx_idx (e.g. 3 for EP1 IN) */
    mmwr(hc->db_base, slot * 4u, ep_ctx_idx);
    return xhci_poll_event(hc, TRB_EVT_TRANSFER, (void*)0) == 0 ? (int)len : -1;
}

/* =========================================================================
 * High-level USB enumeration helpers (controller-agnostic wrappers)
 * ========================================================================= */

typedef enum { HC_UHCI, HC_OHCI, HC_XHCI } hc_type_t;

typedef struct {
    hc_type_t type;
    u16       uhci_base;   /* UHCI only */
    u32       mmio_base;   /* OHCI / xHCI */
    u32      *uhci_fl;     /* UHCI frame list */
    ohci_ed_t *ohci_ed_ctrl;
    ohci_ed_t *ohci_ed_intr;
    u8        addr;        /* USB device address assigned */
    u8        intr_ep;     /* interrupt endpoint number */
    u16       intr_mps;    /* max packet size */
    u8        intr_interval;
    u8        data_toggle;
    int       ls;          /* low-speed flag */
    u8        xhci_slot;
    u8        xhci_ep_ctx; /* context index for interrupt EP */
} usb_hc_t;

static usb_setup_t *make_setup(u8 bmRT, u8 bReq, u16 wVal, u16 wIdx, u16 wLen){
    usb_setup_t *s = (usb_setup_t*)ka_alloc(sizeof(usb_setup_t));
    if(!s) return (void*)0;
    s->bmRequestType = bmRT;
    s->bRequest      = bReq;
    s->wValue        = wVal;
    s->wIndex        = wIdx;
    s->wLength       = wLen;
    return s;
}

static int hc_control(usb_hc_t *hc,
                        u8 bmRT, u8 bReq, u16 wVal, u16 wIdx,
                        void *data, u16 dlen, int is_in){
    usb_setup_t *s = make_setup(bmRT, bReq, wVal, wIdx, dlen);
    if(!s) return -1;
    switch(hc->type){
    case HC_UHCI:
        return uhci_control(hc->uhci_base, hc->uhci_fl,
                            hc->addr, 0, s, data, dlen, is_in, hc->ls);
    case HC_OHCI:
        return ohci_control(hc->mmio_base, hc->ohci_ed_ctrl,
                            hc->addr, 0, s, data, dlen, is_in, hc->ls);
    case HC_XHCI:
        return xhci_control(&g_xhc, hc->xhci_slot, s, data, dlen, is_in);
    }
    return -1;
}

static int hc_intr_in(usb_hc_t *hc, void *buf, u16 len){
    switch(hc->type){
    case HC_UHCI:
        return uhci_intr_in(hc->uhci_base, hc->uhci_fl,
                            hc->addr, hc->intr_ep,
                            buf, len, &hc->data_toggle, hc->ls);
    case HC_OHCI:
        return ohci_intr_in(hc->mmio_base, hc->ohci_ed_intr,
                            hc->addr, hc->intr_ep,
                            buf, len, &hc->data_toggle, hc->ls);
    case HC_XHCI:
        return xhci_intr_in(&g_xhc, hc->xhci_slot, hc->xhci_ep_ctx,
                            buf, len, &hc->data_toggle);
    }
    return -1;
}

/* =========================================================================
 * USB keyboard enumeration (runs once per detected port)
 *
 * Steps:
 *   1. GET_DESCRIPTOR(Device)  — confirm bMaxPacketSize0, optionally VID/PID
 *   2. SET_ADDRESS(1)
 *   3. GET_DESCRIPTOR(Config, 255)  — parse for HID interface + interrupt EP
 *   4. SET_CONFIGURATION(bConfigurationValue)
 *   5. SET_PROTOCOL(0)  — boot protocol
 *   6. SET_IDLE(0,0)    — no repeat reports until key changes
 *   7. Schedule first interrupt IN transfer
 * ========================================================================= */

static int enumerate_kbd(usb_hc_t *hc){
    u8 *buf = (u8*)ka_alloc(256);
    if(!buf) return -1;

    /* Step 1: GET_DESCRIPTOR(Device) at address 0 */
    hc->addr = 0;
    int r = hc_control(hc,
                USB_DIR_IN | USB_TYPE_STD | USB_RCPT_DEV,
                USB_REQ_GET_DESCRIPTOR,
                (u16)(USB_DESC_DEVICE << 8), 0,
                buf, 18, 1);
    if(r < 8){
        printk("usb/kbd: GET_DESCRIPTOR(Device) failed (%d)\n", r);
        return -1;
    }
    usb_dev_desc_t *dd = (usb_dev_desc_t*)buf;
    printk("usb/kbd: bcdUSB=0x%04x vid=0x%04x pid=0x%04x mps0=%d\n",
                (int)dd->bcdUSB, (int)dd->idVendor,
                (int)dd->idProduct, (int)dd->bMaxPacketSize0);

    /* Step 2: SET_ADDRESS(1) */
    r = hc_control(hc,
            USB_DIR_OUT | USB_TYPE_STD | USB_RCPT_DEV,
            USB_REQ_SET_ADDRESS,
            1, 0, (void*)0, 0, 0);
    if(r < 0){
        printk("usb/kbd: SET_ADDRESS failed\n");
        return -1;
    }
    hc->addr = 1;
    msleep(2);

    /* Step 3: GET_DESCRIPTOR(Config, 255 bytes) */
    r = hc_control(hc,
            USB_DIR_IN | USB_TYPE_STD | USB_RCPT_DEV,
            USB_REQ_GET_DESCRIPTOR,
            (u16)(USB_DESC_CONFIG << 8), 0,
            buf, 255, 1);
    if(r < 9){
        printk("usb/kbd: GET_DESCRIPTOR(Config) failed (%d)\n", r);
        return -1;
    }

    /* Parse configuration for HID boot-keyboard interface + interrupt EP */
    u8 cfg_val     = 0;
    u8 iface_num   = 0;
    int found_hid  = 0;
    hc->intr_ep    = 0;
    hc->intr_mps   = 8;
    hc->intr_interval = 10;

    {
        int off = 0;
        /* First descriptor is configuration descriptor (bLength=9) */
        if(r >= 9) cfg_val = buf[5]; /* bConfigurationValue */
        off = (int)buf[0]; /* skip config descriptor */

        while(off + 2 <= r){
            u8 dlen  = buf[off];
            u8 dtype = buf[off+1];
            if(dlen < 2) break;

            if(dtype == 0x04 && off + 9 <= r){ /* Interface */
                usb_iface_desc_t *id = (usb_iface_desc_t*)(buf+off);
                if(id->bInterfaceClass    == 3 &&  /* HID */
                   id->bInterfaceSubClass == 1 &&  /* Boot */
                   id->bInterfaceProtocol == 1){   /* Keyboard */
                    found_hid = 1;
                    iface_num = id->bInterfaceNumber;
                    printk("usb/kbd: HID boot keyboard iface %d\n",
                                (int)iface_num);
                }
            }
            if(found_hid && dtype == 0x05 && off + 7 <= r){ /* Endpoint */
                usb_ep_desc_t *ed = (usb_ep_desc_t*)(buf+off);
                /* Interrupt IN endpoint */
                if((ed->bEndpointAddress & 0x80u) &&
                   (ed->bmAttributes & 0x03u) == 3){
                    hc->intr_ep       = ed->bEndpointAddress & 0x0Fu;
                    hc->intr_mps      = ed->wMaxPacketSize;
                    hc->intr_interval = ed->bInterval;
                    printk("usb/kbd: intr EP%d mps=%d interval=%d\n",
                                (int)hc->intr_ep,
                                (int)hc->intr_mps,
                                (int)hc->intr_interval);
                    break;
                }
            }
            off += (int)dlen;
        }
    }

    if(!found_hid || !hc->intr_ep){
        printk("usb/kbd: no HID boot keyboard endpoint found\n");
        return -1;
    }

    /* Step 4: SET_CONFIGURATION */
    r = hc_control(hc,
            USB_DIR_OUT | USB_TYPE_STD | USB_RCPT_DEV,
            USB_REQ_SET_CONFIGURATION,
            cfg_val, 0, (void*)0, 0, 0);
    if(r < 0) printk("usb/kbd: SET_CONFIGURATION warning (%d)\n", r);
    msleep(5);

    /* Step 5: SET_PROTOCOL(0) — boot protocol */
    r = hc_control(hc,
            USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_IFC,
            USB_REQ_SET_PROTOCOL,
            0, iface_num, (void*)0, 0, 0);
    if(r < 0) printk("usb/kbd: SET_PROTOCOL warning (%d)\n", r);

    /* Step 6: SET_IDLE(0, 0) — indefinite idle, report on change only */
    r = hc_control(hc,
            USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_IFC,
            USB_REQ_SET_IDLE,
            0, iface_num, (void*)0, 0, 0);
    if(r < 0) printk("usb/kbd: SET_IDLE warning (%d)\n", r);

    printk("usb/kbd: keyboard enumerated OK on addr=%d ep=%d\n",
                (int)hc->addr, (int)hc->intr_ep);
    return 0;
}

/* =========================================================================
 * Global HC state for the kernel's poll path
 * ========================================================================= */

static usb_hc_t  g_kbd_hc;          /* the HC that owns the keyboard  */
static u8       *g_report_buf;       /* 8-byte HID report scratch buffer */
static int       g_usb_kbd_ready = 0;

/* =========================================================================
 * Public API — symbols consumed by gen/input.c
 * =========================================================================
 *
 *  usb_kbd_is_initialized()  — called from input_init (line 70)
 *  usb_kbd_has_input()       — called from input_poll (line 98)
 *  usb_kbd_getc()            — called from input_poll (line 99)
 *  usb_kbd_get_key_state(sc) — called from input_get_modifiers (line 175)
 *
 * Also kept:
 *  usb_kbd_poll()            — older callers / direct scan-code consumers
 *  usb_kbd_present()         — legacy present check
 * ========================================================================= */

/*
 * Fetch one fresh report from the hardware and decode it into the ring.
 * Safe to call from multiple paths — ring_pop() drains one entry at a time.
 */
static void usb_kbd_fetch(void){
    if(!g_usb_kbd_ready || !g_report_buf) return;
    int r = hc_intr_in(&g_kbd_hc, g_report_buf, 8);
    if(r == 8){
        int changed = 0;
        for(int i = 0; i < 8; i++)
            if(g_report_buf[i] != g_prev_report[i]){ changed = 1; break; }
        if(changed) {
            decode_report(g_report_buf);
        }
    } else if(r < 0) {
        static int fail_count = 0;
        if(fail_count < 5) {
            printk("usb/kbd: fetch failed r=%d\n", r);
            fail_count++;
        }
    }
}

/*
 * usb_kbd_is_initialized() — returns 1 once a keyboard has been found
 * and all HC structures are live.  Called by input_init to decide whether
 * to register the USB keyboard path.
 */
int usb_kbd_is_initialized(void){
    return g_usb_kbd_ready;
}

/*
 * usb_kbd_has_input() — returns 1 if at least one scan-code is waiting
 * in the ring buffer.  Polls the hardware first so latency is minimal.
 */
int usb_kbd_has_input(void){
    usb_kbd_fetch();
    return (g_ring.head != g_ring.tail) ? 1 : 0;
}

/*
 * usb_kbd_getc() — returns the next scan-code from the ring (blocking-style:
 * polls hardware once and returns 0 if nothing arrived).
 * Key-up events are returned as (make-code | 0x80).
 */
int usb_kbd_getc(void){
    usb_kbd_fetch();
    return (int)ring_pop();
}

/*
 * usb_kbd_get_key_state(scancode) — returns 1 if the key with the given
 * Ark PS/2 make-code is currently held down, 0 otherwise.
 * Used by input_get_modifiers to test Shift, Ctrl, Alt etc.
 *
 * We track live state directly from g_prev_report (the last decoded HID
 * report) rather than a separate shadow array, so it is always current.
 */
int usb_kbd_get_key_state(int scancode){
    if(!g_usb_kbd_ready) return 0;

    /* Check modifier byte (g_prev_report[0]) */
    for(int b = 0; b < 8; b++){
        if(hid_mod_ark[b] == (u8)scancode){
            if(g_prev_report[0] & (1u << b)) return 1;
        }
    }

    /* Check keycodes in slots 2..7 */
    for(int i = 2; i < 8; i++){
        u8 kc = g_prev_report[i];
        if(!kc || kc >= 0x74) continue;
        if(hid_to_ark[kc] == (u8)scancode) return 1;
    }
    return 0;
}

/*
 * usb_kbd_poll() — legacy: fetch + return one scan-code.
 */
int usb_kbd_poll(void){
    return usb_kbd_getc();
}

/*
 * usb_kbd_present() — legacy present flag.
 */
int usb_kbd_present(void){ return g_kbd_present; }

/* =========================================================================
 * UHCI host controller bring-up  (USB 1.1)
 * ========================================================================= */

static int uhci_probe_kbd(u16 iobase){
    printk("usb/uhci: probe iobase=0x%x\n", (int)iobase);

    /* 1. Global Reset (10 ms) */
    _outw(iobase + UHCI_CMD, (1u<<2)); msleep(10);
    _outw(iobase + UHCI_CMD, 0);       msleep(10);

    /* 2. HC Reset */
    _outw(iobase + UHCI_CMD, UHCI_CMD_HCRST);
    for(int i=0; i<100 && (_inw(iobase+UHCI_CMD)&UHCI_CMD_HCRST); i++) msleep(1);
    if(_inw(iobase+UHCI_CMD) & UHCI_CMD_HCRST){
        printk("usb/uhci: reset timeout\n"); return -1;
    }

    _outw(iobase + UHCI_STS,  0x3Fu);
    _outw(iobase + UHCI_INTR, 0);
    _outw(iobase + UHCI_FRNUM, 0);

    /* 3. Frame list */
    u32 *fl = (u32*)ka_alloc(1024 * sizeof(u32));
    if(!fl){ printk("usb/uhci: OOM\n"); return -1; }
    for(int i=0;i<1024;i++) fl[i] = UHCI_TD_TERMINATE;

    _outl(iobase + UHCI_FLBASE, kphys(fl));
    _outb(iobase + UHCI_SOF,    0x40u);
    _outw(iobase + UHCI_CMD,    UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
    msleep(5);

    if(_inw(iobase + UHCI_STS) & (1u<<5)){
        printk("usb/uhci: HCHalted after start\n"); return -1;
    }
    printk("usb/uhci: running\n");

    /* 4. Enumerate ports */
    u16 ports[2] = { UHCI_PORT1, UHCI_PORT2 };
    for(int p=0; p<2; p++){
        u16 sc = _inw(iobase + ports[p]);
        if(!(sc & UHCI_PORT_CCS)) continue;

        /* Clear CSC */
        _outw(iobase + ports[p], UHCI_PORT_CSC);
        msleep(2);

        /* Port reset (50 ms) */
        _outw(iobase + ports[p], UHCI_PORT_RST); msleep(50);
        _outw(iobase + ports[p], 0);             msleep(10);

        sc = _inw(iobase + ports[p]);
        sc |= UHCI_PORT_PED;
        _outw(iobase + ports[p], sc);
        msleep(10);

        sc = _inw(iobase + ports[p]);
        if(!(sc & UHCI_PORT_PED)){
            printk("usb/uhci: port%d enable failed\n", p+1);
            continue;
        }
        int ls = (sc & UHCI_PORT_LSDA) ? 1 : 0;
        printk("usb/uhci: port%d device (%s-speed)\n",
                    p+1, ls ? "low" : "full");

        /* Try to enumerate as keyboard */
        usb_hc_t hc = {0};
        hc.type       = HC_UHCI;
        hc.uhci_base  = iobase;
        hc.uhci_fl    = fl;
        hc.ls         = ls;
        hc.data_toggle= 0;
        hc.ohci_ed_ctrl = (void*)0;
        hc.ohci_ed_intr = (void*)0;

        if(enumerate_kbd(&hc) == 0){
            g_kbd_hc        = hc;
            g_report_buf    = (u8*)ka_alloc(8);
            g_usb_kbd_ready = 1;
            g_kbd_present   = 1;
            return 0;
        }
    }
    return -1;
}

/* =========================================================================
 * OHCI host controller bring-up  (USB 1.1)
 * ========================================================================= */

static int ohci_probe_kbd(u32 mmbase){
    printk("usb/ohci: probe mmio=0x%x\n", (int)mmbase);

    /* Take ownership from SMM */
    if(mmrd(mmbase, OHCI_CONTROL) & OHCI_CTRL_IR){
        mmwr(mmbase, OHCI_CMDSTATUS, OHCI_CS_OCR);
        for(int i=0; i<100 && (mmrd(mmbase,OHCI_CONTROL)&OHCI_CTRL_IR); i++) msleep(5);
    }

    u32 fmi = mmrd(mmbase, OHCI_FMINTERVAL);
    if(!(fmi & 0x3FFFu)) fmi = 0xA7782EDFu;

    /* SW reset */
    mmwr(mmbase, OHCI_CMDSTATUS, OHCI_CS_HCR);
    for(int i=0; i<30 && (mmrd(mmbase,OHCI_CMDSTATUS)&OHCI_CS_HCR); i++) msleep(1);
    msleep(10);

    mmwr(mmbase, OHCI_FMINTERVAL,  fmi);
    mmwr(mmbase, OHCI_PERIODICST, ((fmi & 0x3FFFu) * 9u) / 10u);

    ohci_hcca_t *hcca = (ohci_hcca_t*)ka_alloc(sizeof(ohci_hcca_t));
    if(!hcca){ printk("usb/ohci: OOM\n"); return -1; }
    for(int i=0;i<32;i++) hcca->intr[i] = 1u;
    mmwr(mmbase, OHCI_HCCA, kphys(hcca));

    mmwr(mmbase, 0x14u /*INTRDISABLE*/, 0xFFFFFFFFu);
    mmwr(mmbase, 0x0Cu /*INTRSTATUS*/, 0xFFFFFFFFu);

    u32 ctrl = mmrd(mmbase, OHCI_CONTROL);
    ctrl &= ~OHCI_CTRL_HCFS_MASK;
    ctrl |= OHCI_CTRL_HCFS_OPER | (1u<<2) /*PLE*/;
    mmwr(mmbase, OHCI_CONTROL, ctrl);
    msleep(5);
    printk("usb/ohci: running\n");

    /* Power ports */
    u32 rha = mmrd(mmbase, OHCI_RHDESCRA);
    int nports = (int)(rha & 0xFFu);
    int nps    = (int)((rha>>9)&1);
    if(!nps){ mmwr(mmbase, OHCI_RHSTATUS, (1u<<16)); msleep(50); }
    printk("usb/ohci: %d port(s)\n", nports);

    ohci_ed_t *ed_ctrl = (ohci_ed_t*)ka_alloc(sizeof(ohci_ed_t));
    ohci_ed_t *ed_intr = (ohci_ed_t*)ka_alloc(sizeof(ohci_ed_t));
    if(!ed_ctrl || !ed_intr){ printk("usb/ohci: OOM\n"); return -1; }

    for(int p=0; p<nports; p++){
        u32 ps = mmrd(mmbase, OHCI_RHPORT(p));
        if(!(ps & OHCI_PORT_CCS)) continue;

        mmwr(mmbase, OHCI_RHPORT(p), OHCI_PORT_PRS);
        for(int i=0; i<100 && (mmrd(mmbase,OHCI_RHPORT(p))&OHCI_PORT_PRS); i++) msleep(1);
        mmwr(mmbase, OHCI_RHPORT(p), OHCI_PORT_PRSC);
        msleep(10);

        ps = mmrd(mmbase, OHCI_RHPORT(p));
        if(!(ps & OHCI_PORT_PES)){
            printk("usb/ohci: port%d enable failed\n", p+1);
            continue;
        }
        int ls = (ps & OHCI_PORT_LSDA) ? 1 : 0;
        printk("usb/ohci: port%d device (%s-speed)\n",
                    p+1, ls ? "low" : "full");

        usb_hc_t hc = {0};
        hc.type          = HC_OHCI;
        hc.mmio_base     = mmbase;
        hc.ohci_ed_ctrl  = ed_ctrl;
        hc.ohci_ed_intr  = ed_intr;
        hc.ls            = ls;
        hc.data_toggle   = 0;

        if(enumerate_kbd(&hc) == 0){
            g_kbd_hc        = hc;
            g_report_buf    = (u8*)ka_alloc(8);
            g_usb_kbd_ready = 1;
            g_kbd_present   = 1;
            return 0;
        }
    }
    return -1;
}

/* =========================================================================
 * PCI scan for all USB host controllers
 * ========================================================================= */

static int xhci_probe_kbd(u32 cap_base){
    printk("usb/xhci: probe mmio=0x%x\n", (int)cap_base);

    u8  caplength = (u8)(mmrd(cap_base, XCAP_CAPLENGTH) & 0xFFu);
    u16 hciver    = (u16)(mmrd(cap_base, XCAP_HCIVERSION) >> 16);
    u32 hcsp1     = mmrd(cap_base, XCAP_HCSPARAMS1);
    u32 dboff     = mmrd(cap_base, XCAP_DBOFF)  & ~3u;
    u32 rtsoff    = mmrd(cap_base, XCAP_RTSOFF) & ~0x1Fu;

    u32 op  = cap_base + caplength;
    u32 rt  = cap_base + rtsoff;
    u32 db  = cap_base + dboff;

    int max_ports = (int)((hcsp1>>24)&0xFFu);
    int max_slots = (int)(hcsp1 & 0xFFu);

    printk("usb/xhci: version %x.%02x  ports=%d slots=%d\n",
                (int)(hciver>>8),(int)(hciver&0xFF), max_ports, max_slots);

    /* Wait for CNR */
    for(int i=0; i<100 && (mmrd(op,XOP_USBSTS)&XSTS_CNR); i++) msleep(1);

    /* Stop */
    mmwr(op, XOP_USBCMD, mmrd(op,XOP_USBCMD) & ~XCMD_RUN);
    for(int i=0; i<100 && !(mmrd(op,XOP_USBSTS)&XSTS_HCH); i++) msleep(1);

    /* Reset */
    mmwr(op, XOP_USBCMD, mmrd(op,XOP_USBCMD) | XCMD_HCRST);
    for(int i=0; i<200 && (mmrd(op,XOP_USBCMD)&XCMD_HCRST); i++) msleep(1);
    for(int i=0; i<100 && (mmrd(op,XOP_USBSTS)&XSTS_CNR);  i++) msleep(1);
    msleep(50);
    printk("usb/xhci: reset OK\n");

    /* Max slots */
    mmwr(op, XOP_CONFIG, (mmrd(op,XOP_CONFIG)&~0xFFu)|(u32)max_slots);

    /* DCBAA */
    u32 *dcbaa = (u32*)ka_alloc((u32)(max_slots+1)*8u);
    if(!dcbaa){ printk("usb/xhci: OOM\n"); return -1; }
    mmwr(op, XOP_DCBAAP,   kphys(dcbaa));
    mmwr(op, XOP_DCBAAP+4, 0);

    /* Command Ring */
    xhci_trb_t *cmd_ring = (xhci_trb_t*)ka_alloc(XHCI_CMD_RING_SZ*sizeof(xhci_trb_t));
    if(!cmd_ring){ printk("usb/xhci: OOM\n"); return -1; }
    /* Link TRB at end */
    cmd_ring[XHCI_CMD_RING_SZ-1u].p0 = kphys(cmd_ring);
    cmd_ring[XHCI_CMD_RING_SZ-1u].p1 = 0;
    cmd_ring[XHCI_CMD_RING_SZ-1u].p2 = 0;
    cmd_ring[XHCI_CMD_RING_SZ-1u].p3 = (TRB_LINK<<TRB_TYPE_SHIFT)|TRB_TC|1u;
    mmwr(op, XOP_CRCR,   kphys(cmd_ring)|1u);
    mmwr(op, XOP_CRCR+4, 0);

    /* Event Ring */
    xhci_trb_t *evt_ring = (xhci_trb_t*)ka_alloc(XHCI_EVT_RING_SZ*sizeof(xhci_trb_t));
    xhci_erst_t *erst    = (xhci_erst_t*)ka_alloc(sizeof(xhci_erst_t));
    if(!evt_ring||!erst){ printk("usb/xhci: OOM\n"); return -1; }
    erst->base_lo = kphys(evt_ring);
    erst->base_hi = 0;
    erst->size    = XHCI_EVT_RING_SZ;
    erst->rsvd    = 0;

    u32 ir0 = rt + 0x20u;
    mmwr(ir0, XHCI_IR_ERSTSZ,    1);
    mmwr(ir0, XHCI_IR_ERDP_LO,   kphys(evt_ring)|(1u<<3));
    mmwr(ir0, XHCI_IR_ERDP_HI,   0);
    mmwr(ir0, XHCI_IR_ERSTBA_LO, kphys(erst));
    mmwr(ir0, XHCI_IR_ERSTBA_HI, 0);

    mmwr(op, XOP_DNCTRL, 0xFFFCu);

    /* Keyboard Transfer Ring (used for interrupt EP) */
    xhci_trb_t *kbd_tr = (xhci_trb_t*)ka_alloc(XHCI_TR_RING_SZ*sizeof(xhci_trb_t));
    if(!kbd_tr){ printk("usb/xhci: OOM\n"); return -1; }
    kbd_tr[XHCI_TR_RING_SZ-1u].p0 = kphys(kbd_tr);
    kbd_tr[XHCI_TR_RING_SZ-1u].p1 = 0;
    kbd_tr[XHCI_TR_RING_SZ-1u].p2 = 0;
    kbd_tr[XHCI_TR_RING_SZ-1u].p3 = (TRB_LINK<<TRB_TYPE_SHIFT)|TRB_TC|1u;

    /* Populate g_xhc */
    g_xhc.op_base     = op;
    g_xhc.rt_base     = rt;
    g_xhc.db_base     = db;
    g_xhc.cmd_ring    = cmd_ring;
    g_xhc.cmd_enq     = 0;
    g_xhc.cmd_cycle   = 1;
    g_xhc.evt_ring    = evt_ring;
    g_xhc.evt_deq     = 0;
    g_xhc.evt_cycle   = 1;
    g_xhc.dcbaa       = dcbaa;
    g_xhc.max_slots   = max_slots;
    g_xhc.max_ports   = max_ports;
    g_xhc.kbd_tr      = kbd_tr;
    g_xhc.ep0_tr      = 0;
    g_xhc.kbd_tr_enq  = 0;
    g_xhc.kbd_tr_cycle= 1;

    /* Start HC */
    mmwr(op, XOP_USBCMD, mmrd(op,XOP_USBCMD)|XCMD_RUN);
    for(int i=0; i<100 && (mmrd(op,XOP_USBSTS)&XSTS_HCH); i++) msleep(1);
    if(mmrd(op,XOP_USBSTS)&XSTS_HCH){
        printk("usb/xhci: failed to start\n"); return -1;
    }
    printk("usb/xhci: running\n");

    /* Enumerate ports */
    for(int p=0; p<max_ports; p++){
        u32 ps = mmrd(op, XOP_PORTSC(p));

        /* Power on */
        if(!(ps & XPS_PP)){
            mmwr(op, XOP_PORTSC(p), (ps & ~XPS_W1C)|XPS_PP);
            msleep(20);
            ps = mmrd(op, XOP_PORTSC(p));
        }

        if(!(ps & XPS_CCS)) continue;

        /* Reset port */
        mmwr(op, XOP_PORTSC(p), (ps & ~XPS_W1C)|XPS_PR|XPS_W1C);
        msleep(50);
        for(int i=0; i<100 && !(mmrd(op,XOP_PORTSC(p))&XPS_PRC); i++) msleep(1);
        ps = mmrd(op, XOP_PORTSC(p));
        mmwr(op, XOP_PORTSC(p), (ps & ~XPS_W1C)|XPS_PRC);
        msleep(10);

        ps = mmrd(op, XOP_PORTSC(p));
        u32 speed = XPS_SPEED(ps);
        const char *sname = (speed==4||speed==5) ? "super" :
                            (speed==3)            ? "high"  :
                            (speed==2)            ? "low"   : "full";
        printk("usb/xhci: port%d device (%s-speed)\n", p+1, sname);

        /* Enable Slot command */
        xhci_post_cmd(&g_xhc, 0, 0, 0, (TRB_CMD_ENABLE_SLOT<<TRB_TYPE_SHIFT));
        /* Poll for Command Completion Event; slot ID in event p3[31:24] */
        {
            int found_slot = 0;
            for(int t=0; t<200; t++){
                msleep(1);
                xhci_trb_t *ev = &g_xhc.evt_ring[g_xhc.evt_deq];
                if((ev->p3&1u) != (u32)g_xhc.evt_cycle) continue;
                u32 evtype = (ev->p3>>10u)&0x3Fu;
                u32 cc     = (ev->p2>>24u)&0xFFu;
                u8  slot   = (u8)(ev->p3>>24u);
                g_xhc.evt_deq++;
                if(g_xhc.evt_deq >= XHCI_EVT_RING_SZ){
                    g_xhc.evt_deq = 0; g_xhc.evt_cycle ^= 1u;
                }
                mmwr(rt+0x20u, XHCI_IR_ERDP_LO,
                     kphys(&g_xhc.evt_ring[g_xhc.evt_deq])|(1u<<3));
                if(evtype == TRB_EVT_CMD_COMP && cc == 1 && slot > 0){
                    g_xhc.kbd_slot = slot;
                    found_slot = 1;
                    printk("usb/xhci: slot %d allocated\n",(int)slot);
                }
                break;
            }
            if(!found_slot) continue;
        }

        /* Allocate Device Context and register in DCBAA */
        xhci_dev_ctx_t *dev_ctx = (xhci_dev_ctx_t*)ka_alloc(sizeof(xhci_dev_ctx_t));
        if(!dev_ctx) continue;
        dcbaa[g_xhc.kbd_slot * 2u]     = kphys(dev_ctx);
        dcbaa[g_xhc.kbd_slot * 2u + 1u]= 0;

        /* Allocate EP0 Transfer Ring */
        xhci_trb_t *ep0_tr = (xhci_trb_t*)ka_alloc(XHCI_TR_RING_SZ*sizeof(xhci_trb_t));
        if(!ep0_tr) continue;
        ep0_tr[XHCI_TR_RING_SZ-1u].p0 = kphys(ep0_tr);
        ep0_tr[XHCI_TR_RING_SZ-1u].p3 = (TRB_LINK<<TRB_TYPE_SHIFT)|TRB_TC|1u;
        g_xhc.ep0_tr = ep0_tr; /* store EP0 TR */
        /* Keep kbd_tr for interrupt EP */

        /* Build Input Context for Address Device */
        xhci_input_ctx_t *ictx = (xhci_input_ctx_t*)ka_alloc(sizeof(xhci_input_ctx_t));
        if(!ictx) continue;
        ictx->icc.add_flags  = (1u<<0)|(1u<<1); /* A0=slot, A1=EP0 */
        ictx->icc.drop_flags = 0;

        /* Slot context */
        u32 route = 0; /* root hub port → route string = 0 */
        u32 rport = (u32)(p+1);
        ictx->slot.f1 = route | (speed<<20u) | (1u<<27u); /* ContextEntries=1 */
        ictx->slot.f2 = (rport<<16u);

        /* EP0 context: control, MPS=8 (64 for full/high speed) */
        u32 mps0 = (speed >= 3) ? 64u : 8u;
        ictx->ep[0].f2     = (3u<<3)  /* EPType=Control */
                           | (mps0<<16u);
        ictx->ep[0].deq_lo = kphys(ep0_tr) | 1u; /* DCS=1 */
        ictx->ep[0].deq_hi = 0;
        ictx->ep[0].f5     = 8u; /* Average TRB length */

        /* ADDRESS_DEVICE command */
        xhci_post_cmd(&g_xhc,
                      kphys(ictx), 0, 0,
                      (TRB_CMD_ADDRESS_DEV<<TRB_TYPE_SHIFT)|
                      ((u32)g_xhc.kbd_slot<<TRB_SLOT_SHIFT));
        {
            int ok = 0;
            for(int t=0; t<200; t++){
                msleep(1);
                xhci_trb_t *ev = &g_xhc.evt_ring[g_xhc.evt_deq];
                if((ev->p3&1u)!=(u32)g_xhc.evt_cycle) continue;
                u32 evtype=(ev->p3>>10u)&0x3Fu;
                u32 cc=(ev->p2>>24u)&0xFFu;
                g_xhc.evt_deq++;
                if(g_xhc.evt_deq>=XHCI_EVT_RING_SZ){
                    g_xhc.evt_deq=0; g_xhc.evt_cycle^=1u;
                }
                mmwr(rt+0x20u,XHCI_IR_ERDP_LO,
                     kphys(&g_xhc.evt_ring[g_xhc.evt_deq])|(1u<<3));
                if(evtype==TRB_EVT_CMD_COMP && cc==1) ok=1;
                break;
            }
            if(!ok){ printk("usb/xhci: ADDRESS_DEVICE failed\n"); continue; }
        }
        printk("usb/xhci: device addressed\n");

        /* Now enumerate as standard USB keyboard */
        usb_hc_t hc = {0};
        hc.type         = HC_XHCI;
        hc.mmio_base    = cap_base;
        hc.xhci_slot    = g_xhc.kbd_slot;
        hc.data_toggle  = 0;

        if(enumerate_kbd(&hc) == 0){
            /* Wire up interrupt EP transfer ring */
            /* EP1 IN → ep_ctx_idx = 3 in xHCI (2*ep_num + dir: 2*1+1=3) */
            u8 ep_ctx_idx = (u8)(2u * hc.intr_ep + 1u);
            g_xhc.kbd_ep_ctx_idx = ep_ctx_idx;
            hc.xhci_ep_ctx   = ep_ctx_idx;

            /* Configure Endpoint command: add interrupt EP */
            xhci_input_ctx_t *ictx2 = (xhci_input_ctx_t*)ka_alloc(sizeof(xhci_input_ctx_t));
            if(ictx2){
                ictx2->icc.add_flags  = (1u<<0)|(1u<<(u32)ep_ctx_idx);
                ictx2->slot.f1 = ictx->slot.f1;
                ictx2->slot.f1 = (ictx2->slot.f1 & ~(0x1Fu<<27u))
                                | ((u32)ep_ctx_idx << 27u);
                ictx2->slot.f2 = ictx->slot.f2;

                /* Interrupt IN EP context (index ep_ctx_idx-1 in our array) */
                xhci_ep_ctx_t *epc = &ictx2->ep[ep_ctx_idx-1u];
                epc->f1     = ((u32)hc.intr_interval << 16u);
                epc->f2     = (7u<<3)   /* EPType=Interrupt IN */
                            | (0u<<8)   /* MaxBurstSize=0 */
                            | ((u32)hc.intr_mps << 16u);
                epc->deq_lo = kphys(g_xhc.kbd_tr)|1u;
                epc->deq_hi = 0;
                epc->f5     = hc.intr_mps;

                xhci_post_cmd(&g_xhc,
                              kphys(ictx2), 0, 0,
                              (TRB_CMD_CONFIG_EP<<TRB_TYPE_SHIFT)|
                              ((u32)g_xhc.kbd_slot<<TRB_SLOT_SHIFT));
                /* Wait for completion */
                for(int t=0;t<200;t++){
                    msleep(1);
                    xhci_trb_t *ev=&g_xhc.evt_ring[g_xhc.evt_deq];
                    if((ev->p3&1u)!=(u32)g_xhc.evt_cycle) continue;
                    u32 cc=(ev->p2>>24u)&0xFFu;
                    g_xhc.evt_deq++;
                    if(g_xhc.evt_deq>=XHCI_EVT_RING_SZ){
                        g_xhc.evt_deq=0;g_xhc.evt_cycle^=1u;
                    }
                    mmwr(rt+0x20u,XHCI_IR_ERDP_LO,
                         kphys(&g_xhc.evt_ring[g_xhc.evt_deq])|(1u<<3));
                    if(cc==1) printk("usb/xhci: interrupt EP configured\n");
                    else printk("usb/xhci: configure EP cc=%d\n",(int)cc);
                    break;
                }
            }

            g_kbd_hc        = hc;
            g_report_buf    = (u8*)ka_alloc(8);
            g_usb_kbd_ready = 1;
            g_kbd_present   = 1;
            return 0;
        }
    }
    return -1;
}

/* =========================================================================
 * PCI scan for all USB host controllers
 * ========================================================================= */

#define PCI_CLASS_USB      0x0Cu
#define PCI_SUBCLS_USB     0x03u
#define PCI_PROGIF_UHCI    0x00u
#define PCI_PROGIF_OHCI    0x10u
#define PCI_PROGIF_EHCI    0x20u
#define PCI_PROGIF_XHCI    0x30u

/*
 * usb_kbd_init() — call once from kernel boot after PCI is available.
 *
 * Scans the PCI bus for USB host controllers in priority order:
 *   xHCI first  (handles USB 3.1, 3.0, 2.0, and 1.1 via integrated TT)
 *   EHCI second (USB 2.0; releases companion UHCI/OHCI ports when done)
 *   OHCI third  (USB 1.1 non-Intel)
 *   UHCI last   (USB 1.1 Intel)
 *
 * Stops scanning as soon as a keyboard is successfully enumerated.
 * Sets g_kbd_present = 1 on success.
 */
void usb_kbd_init(void){
    printk("usb/kbd: scanning PCI for USB controllers\n");

    /* Collect controllers, try xHCI first */
    int pass;
    u8 tried_progif[4] = { PCI_PROGIF_XHCI, PCI_PROGIF_EHCI,
                            PCI_PROGIF_OHCI, PCI_PROGIF_UHCI };

    for(pass = 0; pass < 4 && !g_kbd_present; pass++){
        u8 want = tried_progif[pass];
        if(want == PCI_PROGIF_EHCI) continue; /* handled via xHCI companion */

        for(u16 bus=0; bus<256 && !g_kbd_present; bus++){
            for(u8 dev=0; dev<32 && !g_kbd_present; dev++){
                for(u8 fn=0; fn<8 && !g_kbd_present; fn++){
                    u32 id = pci_r32((u8)bus,dev,fn,0x00);
                    if(id==0xFFFFFFFFu){
                        if(fn==0) goto next_dev;
                        continue;
                    }
                    u32 cr = pci_r32((u8)bus,dev,fn,0x08);
                    u8 cls = (u8)((cr>>24)&0xFF);
                    u8 sub = (u8)((cr>>16)&0xFF);
                    u8 pif = (u8)((cr>> 8)&0xFF);

                    if(cls!=PCI_CLASS_USB || sub!=PCI_SUBCLS_USB || pif!=want){
                        if(fn==0){
                            u8 hdr=pci_r32((u8)bus,dev,fn,0x0C)>>16&0xFF;
                            if(!(hdr&0x80)) break;
                        }
                        continue;
                    }

                    pci_enable((u8)bus,dev,fn);
                    printk("usb: PCI %02x:%02x.%x progif=0x%02x\n",
                                (int)bus,(int)dev,(int)fn,(int)pif);

                    switch(pif){
                    case PCI_PROGIF_UHCI:{
                        u16 iobase=0;
                        for(int b=0;b<6;b++){
                            u32 bar=pci_r32((u8)bus,dev,fn,(u8)(0x10+b*4));
                            if((bar&1u) && (bar&0xFFFCu)){
                                iobase=(u16)(bar&0xFFFCu); break;
                            }
                        }
                        if(iobase) uhci_probe_kbd(iobase);
                        break;
                    }
                    case PCI_PROGIF_OHCI:{
                        u32 mm=pci_bar((u8)bus,dev,fn,0);
                        if(mm) ohci_probe_kbd(mm);
                        break;
                    }
                    case PCI_PROGIF_XHCI:{
                        u32 mm=pci_bar((u8)bus,dev,fn,0);
                        if(mm) xhci_probe_kbd(mm);
                        break;
                    }
                    default: break;
                    }

                    if(fn==0){
                        u8 hdr=(u8)(pci_r32((u8)bus,dev,fn,0x0Cu)>>16);
                        if(!(hdr&0x80)) break;
                    }
                    continue;
                    next_dev:;
                }
            }
        }
    }

    if(g_kbd_present){
        printk("usb/kbd: ready\n");
    } else {
        printk("usb/kbd: no USB keyboard found\n");
    }
}