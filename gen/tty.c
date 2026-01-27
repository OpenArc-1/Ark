/**
 * tty.c - TTY and session management
 *
 * Linux-like session IDs (tty0, tty1, ...) and dmesg-style debug output.
 */

#include "ark/tty.h"
#include "ark/printk.h"
#include <stdarg.h>

#define TTY_DEBUG_SCALE 1000000

static u8 tty_used[TTY_MAX_SESSIONS];
static u32 tty_current_sid = 0;
static u32 tty_jiffies = 0;

u32 tty_alloc(void) {
    for (u32 i = 0; i < TTY_MAX_SESSIONS; i++) {
        if (!tty_used[i]) {
            tty_used[i] = 1;
            tty_jiffies++;
            printk("[    %2u.%06u] tty%u: session allocated (session id %u)\n",
                   tty_jiffies / TTY_DEBUG_SCALE,
                   tty_jiffies % TTY_DEBUG_SCALE,
                   (unsigned)i, (unsigned)i);
            return i;
        }
    }
    printk("[tty] no free session slot\n");
    return (u32)-1;
}

void tty_free(u32 sid) {
    if (sid >= TTY_MAX_SESSIONS || !tty_used[sid])
        return;
    tty_used[sid] = 0;
    tty_jiffies++;
    printk("[    %2u.%06u] tty%u: session released\n",
           tty_jiffies / TTY_DEBUG_SCALE,
           tty_jiffies % TTY_DEBUG_SCALE,
           (unsigned)sid);
    if (tty_current_sid == sid)
        tty_current_sid = 0;
}

u32 tty_current(void) {
    return tty_current_sid;
}

void tty_switch(u32 sid) {
    if (sid < TTY_MAX_SESSIONS && tty_used[sid])
        tty_current_sid = sid;
}

void tty_get_name(u32 sid, char *out, u32 max) {
    if (!out || max < 6)
        return;
    if (sid >= TTY_MAX_SESSIONS) {
        out[0] = '?';
        out[1] = '\0';
        return;
    }
    out[0] = 't';
    out[1] = 't';
    out[2] = 'y';
    out[3] = '0' + (char)sid;
    out[4] = '\0';
}

u8 tty_valid(u32 sid) {
    return sid < TTY_MAX_SESSIONS && tty_used[sid];
}

void tty_debug(u32 sid, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    tty_jiffies++;
   // printk("[    %2u.%06u] tty%u: ",
     //      (unsigned)(tty_jiffies / TTY_DEBUG_SCALE),
       //   (unsigned)(tty_jiffies % TTY_DEBUG_SCALE),
        //   (unsigned)sid);
    vprintk(fmt, ap);
    printk("\n");
    va_end(ap);
}
