/**
 * kbd100.c - PS/2 Keyboard Driver for Ark kernel
 *
 * Simple keyboard driver using direct scan code to ASCII mapping
 */

#include "ark/types.h"
#include "ark/printk.h"

/* I/O ports */
#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64

/* Arrow key codes */
#define ARROW_UP     1
#define ARROW_DOWN   2
#define ARROW_LEFT   3
#define ARROW_RIGHT  4

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
static bool caps = false;

/* Input buffer */
#define KBD_BUFFER_SIZE 256
static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_buffer_head = 0;
static int kbd_buffer_tail = 0;

/**
 * inb - Read byte from I/O port
 */
static inline u8 inb(u16 port)
{
    u8 value;
    asm volatile("inb %w1, %b0" : "=a"(value) : "Nd"(port));
    return value;
}

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
    char c = kbd_buffer[kbd_buffer_tail];
    kbd_buffer_tail = (kbd_buffer_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

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
}
