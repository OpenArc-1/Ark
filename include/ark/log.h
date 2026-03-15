#pragma once

#include "ark/types.h"

/*
 * Simple kernel logging subsystem that can mirror printk output into a
 * ramfs-backed text file.  The facility is deliberately lightweight and
 * does not depend on the VFS write path (which is currently read-only).
 *
 * The script interpreter (ks/script.c) understands a "log:<path>"
 * command which calls log_open() to begin capturing messages.  Once a log
 * is open, every character that goes through printk() will also be
 * appended to the in‑memory buffer.  The buffer is exposed to userspace
 * via the ramfs so it can be read later (e.g. from an init script or a
 * user program).
 */

/**
 * Open (or re-open) the named log file.  If the file does not already
 * exist in ramfs an entry will be created.  Calling this while a log is
 * already active will switch the target path and start writing at the
 * beginning of the new buffer.
 */
void log_open(const char *path);

/**
 * Append a single character to the active log (only meaningful if
 * log_open() has been called earlier).
 */
void log_putchar(char c);

/**
 * Direct buffer write helper (used by the printf machinery).
 */
void log_write(const char *data, u32 len);

/**
 * Close the current log.  Future printk() calls will no longer be
 * captured until another log_open() occurs.
 */
void log_close(void);
