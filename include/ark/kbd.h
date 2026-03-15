/**
 * include/ark/kbd.h — Ark keyboard subsystem header
 *
 * Dual-use: included by kernel drivers AND userspace programs.
 *
 * Kernel code (compiled with -D_ARK_KERNEL):
 *   - KEY_* scan-code constants (bare names, used by kbd100.c / usb_kbd.c)
 *   - kbd_irq_pending extern
 *   - kput() / kbd_poll() / kbd_init() declarations
 *
 * Userspace code (ark-gcc programs):
 *   - ARK_KEY_* constants (same values, ARK_ prefix)
 *   - ark_kbd_poll() via direct i8042 port read (PS/2) or unified
 *     kernel read — whichever is available at runtime
 *   - ark_kbd_getchar(), ark_kbd_scancode_to_ascii(), ark_kbd_held()
 */
#ifndef ARK_KBD_H
#define ARK_KBD_H

#include <ark/types.h>

/* =========================================================================
 * PS/2 set-1 scan-code make-codes
 * Defined once under both KEY_* (kernel) and ARK_KEY_* (userspace) names.
 * ========================================================================= */

#define KEY_ESC           0x01
#define KEY_BACKSPACE     0x0E
#define KEY_TAB           0x0F
#define KEY_ENTER         0x1C
#define KEY_LCTRL         0x1D
#define KEY_LSHIFT        0x2A
#define KEY_RSHIFT        0x36
#define KEY_LALT          0x38
#define KEY_SPACE         0x39
#define KEY_CAPS          0x3A
#define KEY_F1            0x3B
#define KEY_F2            0x3C
#define KEY_F3            0x3D
#define KEY_F4            0x3E
#define KEY_F5            0x3F
#define KEY_F6            0x40
#define KEY_F7            0x41
#define KEY_F8            0x42
#define KEY_F9            0x43
#define KEY_F10           0x44
#define KEY_F11           0x57
#define KEY_F12           0x58
#define KEY_ARROW_UP      0x48
#define KEY_ARROW_LEFT    0x4B
#define KEY_ARROW_RIGHT   0x4D
#define KEY_ARROW_DOWN    0x50
#define KEY_HOME          0x47
#define KEY_END           0x4F
#define KEY_PGUP          0x49
#define KEY_PGDN          0x51
#define KEY_INS           0x52
#define KEY_DEL           0x53
#define KEY_RELEASE       0x80  /* ORed with make-code on key-up */

/* ARK_KEY_* aliases for userspace source compatibility */
#define ARK_KEY_NONE      0
#define ARK_KEY_ESC       KEY_ESC
#define ARK_KEY_BACKSPACE KEY_BACKSPACE
#define ARK_KEY_TAB       KEY_TAB
#define ARK_KEY_ENTER     KEY_ENTER
#define ARK_KEY_LCTRL     KEY_LCTRL
#define ARK_KEY_LSHIFT    KEY_LSHIFT
#define ARK_KEY_RSHIFT    KEY_RSHIFT
#define ARK_KEY_LALT      KEY_LALT
#define ARK_KEY_SPACE     KEY_SPACE
#define ARK_KEY_CAPS      KEY_CAPS
#define ARK_KEY_F1        KEY_F1
#define ARK_KEY_F2        KEY_F2
#define ARK_KEY_F3        KEY_F3
#define ARK_KEY_F4        KEY_F4
#define ARK_KEY_F5        KEY_F5
#define ARK_KEY_F6        KEY_F6
#define ARK_KEY_F7        KEY_F7
#define ARK_KEY_F8        KEY_F8
#define ARK_KEY_F9        KEY_F9
#define ARK_KEY_F10       KEY_F10
#define ARK_KEY_F11       KEY_F11
#define ARK_KEY_F12       KEY_F12
#define ARK_KEY_UP        KEY_ARROW_UP
#define ARK_KEY_LEFT      KEY_ARROW_LEFT
#define ARK_KEY_RIGHT     KEY_ARROW_RIGHT
#define ARK_KEY_DOWN      KEY_ARROW_DOWN
#define ARK_KEY_DELETE    KEY_DEL
#define ARK_KEY_RELEASE   KEY_RELEASE

/* ── Modifier bitmask ─────────────────────────────────────────────────── */
#define ARK_MOD_LSHIFT  (1 << 0)
#define ARK_MOD_RSHIFT  (1 << 1)
#define ARK_MOD_SHIFT   (ARK_MOD_LSHIFT | ARK_MOD_RSHIFT)
#define ARK_MOD_LCTRL   (1 << 2)
#define ARK_MOD_RCTRL   (1 << 3)
#define ARK_MOD_CTRL    (ARK_MOD_LCTRL | ARK_MOD_RCTRL)
#define ARK_MOD_LALT    (1 << 4)
#define ARK_MOD_RALT    (1 << 5)
#define ARK_MOD_ALT     (ARK_MOD_LALT  | ARK_MOD_RALT)
#define ARK_MOD_CAPS    (1 << 6)

/* =========================================================================
 * Kernel-internal declarations  (-D_ARK_KERNEL)
 * ========================================================================= */
#ifdef _ARK_KERNEL

/*
 * Set to 1 by the PS/2 IRQ handler when a byte is waiting in port 0x60.
 * Cleared by kbd100's poll path after the byte is consumed.
 */
extern volatile int kbd_irq_pending;

/*
 * kput(scancode) — push one decoded PS/2 set-1 scan-code into the kernel
 * input ring (gen/input.c).  Both kbd100.c (PS/2) and usb_kbd.c (USB)
 * call this so all key events share one queue.
 */
void kput(int scancode);

/* kbd_poll() — drain one byte from i8042, decode, call kput(). */
int  kbd_poll(void);

/* kbd_init() — initialise i8042 PS/2 controller at boot. */
void kbd_init(void);

#endif /* _ARK_KERNEL */

/* =========================================================================
 * Userspace API  (ark-gcc programs, no -D_ARK_KERNEL)
 * ========================================================================= */
#ifndef _ARK_KERNEL

/*
 * ark_kbd_init() — no-op.
 * Both PS/2 and USB keyboards are initialised by the kernel before any
 * userspace process starts.  Kept for source compatibility.
 */
static inline void ark_kbd_init(void) {}

/*
 * ark_kbd_poll() — read one scan-code from the i8042 data port.
 * Non-blocking; returns 0 if nothing is waiting.
 * USB key events are merged into the same stream by the kernel's
 * input layer before they reach the port, so this call covers both.
 *
 * Returns make-code on key-down, (make-code | ARK_KEY_RELEASE) on key-up.
 */
static inline int ark_kbd_poll(void) {
    /* Read status port 0x64; bit 0 = Output Buffer Full */
    unsigned char status;
    __asm__ volatile("inb $0x64, %0" : "=a"(status));
    if (!(status & 0x01)) return 0;
    /* Read scan-code from data port 0x60 */
    unsigned char sc;
    __asm__ volatile("inb $0x60, %0" : "=a"(sc));
    return (int)sc;
}

/*
 * ark_kbd_held(scancode) — return 1 if make-code is currently held.
 * Maintains a local shadow of pressed keys updated by ark_kbd_poll().
 * Defined in libark/kbd.c.
 */
int ark_kbd_held(int scancode);

/*
 * ark_kbd_getchar() — block until a printable ASCII key is pressed.
 * Handles Shift and CapsLock.  Defined in libark/kbd.c.
 */
int ark_kbd_getchar(void);

/*
 * ark_kbd_scancode_to_ascii(scancode, shift) — translate make-code to ASCII.
 * Returns 0 for non-printable keys.  Defined in libark/kbd.c.
 */
char ark_kbd_scancode_to_ascii(int scancode, int shift);

#endif /* !_ARK_KERNEL */

#endif /* ARK_KBD_H */