/**
 * input.h - Input device manager for Ark kernel
 *
 * Public API for unified input handling from keyboard and other HID devices.
 */

#ifndef INPUT_H
#define INPUT_H

#include "ark/types.h"

/* Arrow key values */
#define ARROW_UP     1
#define ARROW_DOWN   2
#define ARROW_LEFT   3
#define ARROW_RIGHT  4

/* Input callback function pointer type */
typedef void (*input_callback_t)(char key);

/**
 * input_init - Initialize input subsystem
 *
 * Must be called during kernel initialization before other input functions.
 */
void input_init(void);

/**
 * input_register_handler - Register callback for input events
 *
 * Args:
 *   handler - Callback function to handle input events (or NULL to clear)
 */
void input_register_handler(input_callback_t handler);

/**
 * input_poll - Poll all input devices
 *
 * Should be called regularly from the kernel's main event loop.
 * Processes pending keyboard input and other device events.
 */
void input_poll(void);

/**
 * input_get_modifiers - Get current modifier key states
 *
 * Args:
 *   shift - Pointer to bool (set to true if Shift is pressed)
 *   ctrl - Pointer to bool (set to true if Ctrl is pressed)
 *   alt - Pointer to bool (set to true if Alt is pressed)
 */
void input_get_modifiers(bool *shift, bool *ctrl, bool *alt);

/**
 * input_is_ready - Check if input subsystem is initialized
 *
 * Returns:
 *   true if input subsystem is ready, false otherwise
 */
bool input_is_ready(void);

/**
 * input_read - Read a line of input from keyboard
 *
 * Reads user input character by character until Enter is pressed.
 * Supports backspace for character deletion.
 *
 * Args:
 *   buffer - Destination buffer for input string (null-terminated)
 *   max_len - Maximum length including null terminator
 *   hide_input - If true, display '*' instead of characters (for passwords)
 *
 * Example:
 *   char name[64];
 *   input_read(name, sizeof(name), false);
 *
 *   char password[64];
 *   input_read(password, sizeof(password), true);
 */
void input_read(char *buffer, int max_len, bool hide_input);

/**
 * input_getc - Read a single character or special key code
 *
 * Blocks until a key is pressed. Returns:
 *   - Regular ASCII character (a-z, 0-9, etc.)
 *   - Special key codes: ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT
 *   - Backspace (0x08)
 *   - Enter (0x0A)
 *
 * Example:
 *   char c = input_getc();
 *   if (c == ARROW_UP) { ... }
 */
char input_getc(void);

/**
 * input_has_key - Check if a key is waiting
 *
 * Returns true if input is available without blocking
 */
bool input_has_key(void);

#endif /* INPUT_H */
