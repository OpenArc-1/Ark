/**
 * tty.h - TTY and session management for Ark
 *
 * Linux-like TTY abstraction: each session has a unique ID (tty0, tty1, ...).
 * Use tty_alloc() for new sessions, tty_debug() for dmesg-style debug output.
 */

#pragma once

#include "ark/types.h"

#define TTY_MAX_SESSIONS  8
#define TTY_NAME_MAX      16

/**
 * Allocate a new TTY session.
 * @return Session ID (0..TTY_MAX_SESSIONS-1) on success, (u32)-1 on failure
 */
u32 tty_alloc(void);

/**
 * Release a TTY session.
 * @param sid Session ID from tty_alloc()
 */
void tty_free(u32 sid);

/**
 * Get current TTY session ID.
 * @return Current session ID, or 0 if none set
 */
u32 tty_current(void);

/**
 * Switch current TTY session.
 * @param sid Session ID
 */
void tty_switch(u32 sid);

/**
 * Get TTY name (e.g. "tty0") for a session.
 * @param sid Session ID
 * @param out Buffer to write name (NUL-terminated)
 * @param max  Buffer size including NUL
 */
void tty_get_name(u32 sid, char *out, u32 max);

/**
 * Linux-like debug print for a TTY session.
 * Format: [    0.XXXXXX] ttyN: fmt...
 * @param sid Session ID
 * @param fmt printf-style format
 */
void tty_debug(u32 sid, const char *fmt, ...);

/**
 * Check if a session ID is valid (allocated).
 */
u8 tty_valid(u32 sid);
