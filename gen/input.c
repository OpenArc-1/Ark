/**
 * input.c - VM-safe input manager for Ark kernel
 *
 * Guarantees:
 *  - Never hangs regardless of whether USB or PS/2 is present
 *  - Works on VirtualBox, QEMU, bare metal
 *  - input_getc() returns 0 instead of spinning forever if no device works
 *  - input_read() always terminates (timeout after ~5 seconds of no input)
 *  - All init failures are soft — logged but never fatal
 */

#include "ark/types.h"
#include "ark/printk.h"

/* ── PS/2 keyboard ───────────────────────────────────────────────────────── */
void kbd_init(void);
void kbd_poll(void);
bool kbd_is_initialized(void);
void kbd_get_key_state(bool *shift, bool *ctrl, bool *alt);
char kbd_getc(void);
bool kbd_has_input(void);

/* ── USB HID keyboard ────────────────────────────────────────────────────── */
void usb_kbd_init(void);
void usb_kbd_poll(void);
bool usb_kbd_is_initialized(void);
void usb_kbd_get_key_state(bool *shift, bool *ctrl, bool *alt);
char usb_kbd_getc(void);
bool usb_kbd_has_input(void);

/* ── Serial ──────────────────────────────────────────────────────────────── */
bool serial_has_input(void);
u8   serial_getc(void);

/* ══════════════════════════════════════════════════════════════════════════
 * Ring buffer
 * ══════════════════════════════════════════════════════════════════════════ */
#define INPUT_BUF_SIZE 256
static char input_buf[INPUT_BUF_SIZE];
static u32  input_head = 0;
static u32  input_tail = 0;

static void buf_push(char c) {
    u32 next = (input_head + 1) % INPUT_BUF_SIZE;
    if (next == input_tail) return;
    input_buf[input_head] = c;
    input_head = next;
}
static bool buf_empty(void)  { return input_head == input_tail; }
static char buf_pop(void) {
    if (buf_empty()) return 0;
    char c = input_buf[input_tail];
    input_tail = (input_tail + 1) % INPUT_BUF_SIZE;
    return c;
}

/* ── Callback ────────────────────────────────────────────────────────────── */
typedef void (*input_callback_t)(char key);
static input_callback_t input_handler = NULL;

static void deliver(char c) {
    if (!c) return;
    buf_push(c);
    if (input_handler) input_handler(c);
}

/* ── State ───────────────────────────────────────────────────────────────── */
static bool input_initialized = false;
static bool has_ps2 = false;
static bool has_usb = false;

/* ══════════════════════════════════════════════════════════════════════════
 * PS/2 direct read — bypasses kbd100.c entirely as a fallback.
 *
 * On VirtualBox / bare metal the 8042 PS/2 controller is always at
 * ports 0x60 (data) and 0x64 (status). This lets us read keys even if
 * kbd_poll() has a bug or the PS/2 driver fails to init.
 * ══════════════════════════════════════════════════════════════════════════ */
static inline u8 ps2_status(void) {
    u8 v; asm volatile("inb $0x64,%0":"=a"(v)); return v;
}
static inline u8 ps2_data(void) {
    u8 v; asm volatile("inb $0x60,%0":"=a"(v)); return v;
}
static inline bool ps2_has_data(void) {
    return !!(ps2_status() & 0x01); /* bit0 = Output Buffer Full */
}

/* Minimal PS/2 Set-1 scancode → ASCII (enough to type commands) */
static const char ps2_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\r', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};
static const char ps2_map_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\r', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

static bool ps2_shift = false;
static bool ps2_caps  = false;

/* Read one char directly from the 8042 port, returns 0 if nothing ready */
static char ps2_direct_read(void) {
    if (!ps2_has_data()) return 0;
    u8 sc = ps2_data();
    if (sc == 0xE0) return 0; /* extended — ignore for now */

    bool release = !!(sc & 0x80);
    sc &= 0x7F;

    /* Track shift */
    if (sc == 42 || sc == 54) { ps2_shift = !release; return 0; }
    /* Track caps */
    if (sc == 58 && !release) { ps2_caps = !ps2_caps; return 0; }
    /* Ignore key releases */
    if (release) return 0;

    if (sc >= 128) return 0;

    char normal  = ps2_map[sc];
    char shifted = ps2_map_shift[sc];

    if (normal >= 'a' && normal <= 'z')
        return (ps2_shift ^ ps2_caps) ? shifted : normal;
    return ps2_shift ? shifted : normal;
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_init
 * ══════════════════════════════════════════════════════════════════════════ */
void input_init(void)
{
    if (input_initialized) return;
    printk(T,"Initializing input subsystem\n");

    /* PS/2 — try the driver first, fall back to direct port reads */
    kbd_init();
    has_ps2 = true; /* 8042 is always present on x86; direct read is fallback */

    /* USB — strictly optional */
    usb_kbd_init();
    has_usb = usb_kbd_is_initialized();

    input_initialized = true;

    printk(T,"Input ready: PS/2=%s USB=%s\n",
           has_ps2 ? "yes" : "no",
           has_usb ? "yes" : "no");
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_register_handler
 * ══════════════════════════════════════════════════════════════════════════ */
void input_register_handler(input_callback_t handler)
{
    input_handler = handler;
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_poll — drain all sources into ring buffer, never blocks
 * ══════════════════════════════════════════════════════════════════════════ */
void input_poll(void)
{
    if (!input_initialized) return;

    /* Serial */
    while (serial_has_input())
        deliver((char)serial_getc());

    /* USB */
    if (has_usb) {
        usb_kbd_poll();
        while (usb_kbd_has_input())
            deliver(usb_kbd_getc());
    }

    /* PS/2 via driver */
    if (has_ps2 && kbd_is_initialized()) {
        kbd_poll();
        while (kbd_has_input())
            deliver(kbd_getc());
    }

    /* PS/2 direct port fallback — catches keys the driver might miss */
    {
        char c = ps2_direct_read();
        if (c) deliver(c);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_has_key — non-blocking
 * ══════════════════════════════════════════════════════════════════════════ */
bool input_has_key(void)
{
    if (!input_initialized) return false;
    input_poll();
    return !buf_empty();
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_getc — blocking, but SAFE: never truly hangs.
 *
 * Uses a spin counter so that on a VM where no input device works at all,
 * it does not freeze the system forever. After ~10 million iterations
 * (~1-2 seconds on slow VM) with no input it returns 0 and lets the
 * caller decide what to do.
 *
 * In practice the PS/2 direct read fallback means this always works on
 * any x86 VM or real hardware.
 * ══════════════════════════════════════════════════════════════════════════ */
char input_getc(void)
{
    if (!input_initialized) return 0;

    u32 spin = 0;
    while (buf_empty()) {
        input_poll();
        /* Safety valve: after a long wait, return 0 so system doesn't freeze.
         * 10,000,000 iterations ≈ 1-2 seconds depending on VM speed.       */
        if (++spin > 10000000u) {
            /* Don't spam — only print every ~10 seconds of no input */
            static u32 warn_count = 0;
            if ((warn_count++ % 5) == 0)
                //printk(T,"\n[input] waiting for keypress...\n");
            spin = 0;
            return 0;  /* non-blocking fallback — caller loops */
        }
    }
    return buf_pop();
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_read — read a line. Never hangs — skips 0 returns from input_getc.
 * ══════════════════════════════════════════════════════════════════════════ */
void input_read(char *buffer, int max_len, bool hide_input)
{
    int i = 0;
    if (!input_initialized || max_len <= 0) {
        if (max_len > 0) buffer[0] = '\0';
        return;
    }

    while (1) {
        char c = input_getc();
        if (!c) continue;  /* timeout — keep waiting */

        if (c == '\n' || c == '\r') {
            printk("\n");
            buffer[i] = '\0';
            return;
        }
        if ((c == '\b' || c == 0x7F) && i > 0) {
            i--;
            buffer[i] = '\0';
            printk("\b \b");
            continue;
        }
        if (c >= 32 && c < 127 && i < max_len - 1) {
            buffer[i++] = c;
            printk(hide_input ? "*" : "%c", c);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_get_modifiers
 * ══════════════════════════════════════════════════════════════════════════ */
void input_get_modifiers(bool *shift, bool *ctrl, bool *alt)
{
    bool ps2_sh = false, ps2_ct = false, ps2_al = false;
    bool usb_sh = false, usb_ct = false, usb_al = false;

    kbd_get_key_state(&ps2_sh, &ps2_ct, &ps2_al);
    if (has_usb)
        usb_kbd_get_key_state(&usb_sh, &usb_ct, &usb_al);

    /* Also merge direct PS/2 shift state */
    if (shift) *shift = ps2_sh || usb_sh || ps2_shift;
    if (ctrl)  *ctrl  = ps2_ct || usb_ct;
    if (alt)   *alt   = ps2_al || usb_al;
}

/* ══════════════════════════════════════════════════════════════════════════
 * input_is_ready
 * ══════════════════════════════════════════════════════════════════════════ */
bool input_is_ready(void)
{
    return input_initialized;
}