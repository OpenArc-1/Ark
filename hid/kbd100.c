/**
<<<<<<< HEAD
 * kbd100.c - PS/2 Keyboard Driver for Ark kernel
 *
 * Simple keyboard driver using direct scan code to ASCII mapping
=======
 * kbd100_fixed.c - Full PS/2 Keyboard Driver for Ark kernel
 *
 * Fixed Caps/Shift handling: letters only, special keys mapped properly
 * Supports extended keys and modifiers
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
 */

#include "ark/types.h"
#include "ark/printk.h"
<<<<<<< HEAD

/* I/O ports */
#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64

/* Arrow key codes */
=======
#include <stdbool.h>

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64

/* Custom codes for special keys */
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
#define ARROW_UP     1
#define ARROW_DOWN   2
#define ARROW_LEFT   3
#define ARROW_RIGHT  4
<<<<<<< HEAD

/* Simple lookup table - PS/2 Set 2 scan codes to ASCII */
static const char keymap[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', 0,
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
    'b','n','m',',','.','/', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};

static const char keymap_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b', 0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
    'B','N','M','<','>','?', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};

/* Keyboard state */
static bool shift = false;
=======
#define KEY_HOME     5
#define KEY_END      6
#define KEY_INSERT   7
#define KEY_DELETE   8
#define KEY_PAGEUP   9
#define KEY_PAGEDOWN 10
#define KEY_CLEAR    11

/* Keyboard state */
static bool shift_l = false;
static bool shift_r = false;
static bool ctrl_l = false;
static bool ctrl_r = false;
static bool alt_l = false;
static bool alt_r = false;
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
static bool caps = false;

/* Input buffer */
#define KBD_BUFFER_SIZE 256
static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_buffer_head = 0;
static int kbd_buffer_tail = 0;

<<<<<<< HEAD
/**
 * inb - Read byte from I/O port
 */
static inline u8 inb(u16 port)
{
=======
/* Low-level I/O */
static inline u8 inb(u16 port) {
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
    u8 value;
    asm volatile("inb %w1, %b0" : "=a"(value) : "Nd"(port));
    return value;
}

<<<<<<< HEAD
/**
 * outb - Write byte to I/O port
 */
static inline void outb(u16 port, u8 value)
{
    asm volatile("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

/**
 * Add character to buffer
 */
static void kbd_buffer_put(char c)
{
    int next_head = (kbd_buffer_head + 1) % KBD_BUFFER_SIZE;
    if (next_head == kbd_buffer_tail)
        return;
    kbd_buffer[kbd_buffer_head] = c;
    kbd_buffer_head = next_head;
}

/**
 * Get character from buffer
 */
static char kbd_buffer_get(void)
{
    if (kbd_buffer_head == kbd_buffer_tail)
        return 0;
=======
static inline void outb(u16 port, u8 value) {
    asm volatile("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

/* Add char to buffer */
static void kbd_buffer_put(char c) {
    int next = (kbd_buffer_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_buffer_tail) {
        kbd_buffer[kbd_buffer_head] = c;
        kbd_buffer_head = next;
    }
}

char kbd_buffer_get(void) {
    if (kbd_buffer_head == kbd_buffer_tail) return 0;
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
    char c = kbd_buffer[kbd_buffer_tail];
    kbd_buffer_tail = (kbd_buffer_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

<<<<<<< HEAD
/**
 * Check if buffer has data
 */
static bool kbd_buffer_has_data(void)
{
    return kbd_buffer_head != kbd_buffer_tail;
}

/**
 * Main keyboard polling function - simple and direct
 */
void kbd_poll(void)
{
    static bool extended = false;
    static bool release = false;

    while (inb(KBD_STATUS_PORT) & 1) {
        u8 sc = inb(KBD_DATA_PORT);

        /* Extended code prefix */
        if (sc == 0xE0) {
            extended = true;
            continue;
        }

        /* Release prefix */
        if (sc == 0xF0) {
            release = true;
            continue;
        }

        /* Shift keys on press */
        if ((sc == 0x12 || sc == 0x59) && !release) {
            shift = true;
            release = false;
            continue;
        }

        /* Shift keys on release */
        if ((sc == 0x12 || sc == 0x59) && release) {
            shift = false;
            release = false;
            continue;
        }

        /* Caps lock toggle */
        if (sc == 0x58 && !release) {
            caps = !caps;
        }

        /* Arrow keys */
        if (extended && !release && sc < 128) {
            extended = false;
            switch (sc) {
                case 0x75: kbd_buffer_put(ARROW_UP); break;
                case 0x72: kbd_buffer_put(ARROW_DOWN); break;
                case 0x6B: kbd_buffer_put(ARROW_LEFT); break;
                case 0x74: kbd_buffer_put(ARROW_RIGHT); break;
            }
            release = false;
            continue;
        }

        extended = false;

        /* Regular characters - only on press */
        if (!release && sc < 128) {
            char c;
            char normal = keymap[sc];
            char shifted = keymap_shift[sc];

            if (normal >= 'a' && normal <= 'z') {
                c = (shift ^ caps) ? shifted : normal;
            } else {
                c = shift ? shifted : normal;
            }

            if (c != 0)
                kbd_buffer_put(c);
        }

        release = false;
    }
}

/**
 * Initialize keyboard
 */
void kbd_init(void)
{
    printk("Keyboard initialized\n");
}

/**
 * Get character from keyboard
 */
char kbd_getc(void)
{
    return kbd_buffer_get();
}

/**
 * Check if key waiting
 */
bool kbd_has_input(void)
{
    return kbd_buffer_has_data();
}

/**
 * Get modifier key state
 */
void kbd_get_key_state(bool *shift_key, bool *ctrl_key, bool *alt_key)
{
    if (shift_key) *shift_key = shift;
}

/**
 * Check if initialized
 */
bool kbd_is_initialized(void)
{
    return true;
=======
bool kbd_buffer_has_data(void) {
    return kbd_buffer_head != kbd_buffer_tail;
}

/* Helpers */
static bool is_release(u8 sc) { return sc & 0x80; }
static u8 make_code(u8 sc) { return sc & 0x7F; }

/* PS/2 Set 2 scan code map (simplified, add more if needed) */
static const char keymap[128] = {
    0, 27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0
};

static const char keymap_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0
};

/* Poll keyboard and update state */
void kbd_poll(void)
{
    while (inb(KBD_STATUS_PORT) & 1) {
        u8 sc = inb(KBD_DATA_PORT);

        int released = sc & 0x80;
        sc &= 0x7F;

        /* SHIFT */
        if (sc == 42 || sc == 54) {
            if (released) {
                shift_l = shift_r = false;
            } else {
                if (sc == 42) shift_l = true;
                if (sc == 54) shift_r = true;
            }
            continue;
        }

        /* CAPS LOCK */
        if (sc == 58 && !released) {
            caps = !caps;
            continue;
        }

        if (released) continue;

        if (sc < 128) {
            char normal = keymap[sc];
            char shifted = keymap_shift[sc];
            char c;

            if (normal >= 'a' && normal <= 'z') {
                c = ((shift_l || shift_r) ^ caps) ? shifted : normal;
            } else {
                c = (shift_l || shift_r) ? shifted : normal;
            }

            if (c)
                kbd_buffer_put(c);
        }
    }
}


/* API */
void kbd_init(void) { printk("[ps/2] Keyboard initialized\n"); }
char kbd_getc(void) { return kbd_buffer_get(); }
bool kbd_has_input(void) { return kbd_buffer_has_data(); }
bool kbd_is_initialized(void) { return true; }
void kbd_get_key_state(bool *shift, bool *ctrl, bool *alt) {
    if (shift) *shift = shift_l || shift_r;
    if (ctrl) *ctrl = ctrl_l || ctrl_r;
    if (alt) *alt = alt_l || alt_r;
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
}
