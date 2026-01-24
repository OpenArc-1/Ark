/**
 * input.c - Input device manager for Ark kernel
 *
 * Manages unified input from various devices:
 * - PS/2 Keyboard (kbd100)
 * - Touch input (future)
 * - Other HID devices
 */

#include "ark/types.h"
#include "ark/printk.h"

/* Forward declarations for keyboard driver */
void kbd_init(void);
void kbd_poll(void);
bool kbd_is_initialized(void);
void kbd_get_key_state(bool *shift, bool *ctrl, bool *alt);
char kbd_getc(void);
bool kbd_has_input(void);

/* Serial input functions */
bool serial_has_input(void);
u8 serial_getc(void);

/* Input callback function pointer type */
typedef void (*input_callback_t)(char key);

/* Input subsystem state */
static bool input_initialized = false;
static input_callback_t input_handler = NULL;

/**
 * input_init - Initialize input subsystem
 */
void input_init(void)
{
    if (input_initialized)
        return;

    printk("[    0.001000] Initializing input subsystem\n");
    
    /* Initialize keyboard */
    kbd_init();
    
    input_initialized = true;
    printk("[    0.001100] Input subsystem ready\n");
}

/**
 * input_register_handler - Register callback for input events
 */
void input_register_handler(input_callback_t handler)
{
    input_handler = handler;
    if (handler)
        printk("Input handler registered\n");
}

/**
 * input_poll - Poll all input devices
 *
 * Processes input from both serial and keyboard concurrently.
 * Should be called regularly from the kernel's main loop.
 */
void input_poll(void)
{
    if (!input_initialized)
        return;

    /* Process all available serial input */
    if (serial_has_input()) {
        u8 c = serial_getc();
        if (c && input_handler) {
            input_handler((char)c);
        }
    }

    /* Process keyboard input */
    if (kbd_is_initialized()) {
        kbd_poll();
        if (kbd_has_input()) {
            char c = kbd_getc();
            if (c && input_handler) {
                input_handler(c);
            }
        }
    }

    /* TODO: Poll other input devices (touch, mouse, etc.) */
}

/**
 * input_get_modifiers - Get current modifier key states
 */
void input_get_modifiers(bool *shift, bool *ctrl, bool *alt)
{
    kbd_get_key_state(shift, ctrl, alt);
}

/**
 * input_is_ready - Check if input subsystem is ready
 */
bool input_is_ready(void)
{
    return input_initialized;
}

/**
 * input_read - Read a line of input from keyboard or serial
 *
 * Reads user input character by character until Enter is pressed.
 * Supports backspace for deletion and optional hiding (for passwords).
 * Accepts input from both keyboard and serial and echoes to both via printk.
 *
 * Args:
 *   buffer - Destination buffer for input string (null-terminated)
 *   max_len - Maximum number of characters (including null terminator)
 *   hide_input - If true, display '*' instead of actual characters (for passwords)
 */
void input_read(char *buffer, int max_len, bool hide_input)
{
    int i = 0;
    
    if (!input_initialized) {
        printk("Input subsystem not initialized\n");
        return;
    }
    
    while (1) {
        /* Poll keyboard */
        if (kbd_is_initialized()) {
            kbd_poll();
        }
        
        /* Check for input from serial or keyboard (both sources concurrently) */
        char c = 0;
        if (serial_has_input()) {
            c = (char)serial_getc();
        } else if (kbd_has_input()) {
            c = kbd_getc();
        }
        
        if (!c)
            continue;
        
        /* Handle Enter/Return - finish input */
        if (c == '\n' || c == '\r') {
            printk("\n");
            buffer[i] = '\0';
            break;
        }
        /* Handle Backspace/Delete - don't add to buffer, erase from display */
        else if (c == '\b' || c == 0x7F) {
            if (i > 0) {
                i--;
                buffer[i] = '\0';
                /* Backspace to previous position and erase */
                printk("\b");
            }
        }
        /* Handle regular characters - add to buffer and display */
        else if (c >= 32 && c < 127 && i < max_len - 1) {
            buffer[i++] = c;
            if (hide_input) {
                printk("*");
            } else {
                printk("%c", c);
            }
        }
    }
}

/**
 * input_getc - Read a single character from keyboard or serial
 *
 * Blocks until a key is pressed. Returns:
 *   - Regular ASCII character (a-z, 0-9, etc.)
 *   - Special key codes: ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT
 *   - Backspace (0x08)
 *   - Enter (0x0A)
 *
 * Checks both serial and keyboard input concurrently.
 *
 * Example:
 *   char c = input_getc();
 *   if (c == ARROW_UP) { ... }
 */
char input_getc(void)
{
    if (!input_initialized)
        return 0;
    
    while (1) {
        /* Check serial input and keyboard simultaneously */
        if (serial_has_input()) {
            return (char)serial_getc();
        }
        
        if (kbd_is_initialized()) {
            kbd_poll();
            if (kbd_has_input()) {
                return kbd_getc();
            }
        }
    }
}

/**
 * input_has_key - Check if a key is waiting from keyboard or serial
 *
 * Returns true if input is available without blocking
 */
bool input_has_key(void)
{
    if (!input_initialized)
        return false;
    
    if (serial_has_input())
        return true;
    
    kbd_poll();
    return kbd_has_input();
}
