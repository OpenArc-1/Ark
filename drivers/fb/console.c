/**
 * drivers/fb/console.c — cursor blink tick for Ark kernel console
 *
 * The actual framebuffer console is in gen/printk.c (shadow buffer,
 * correct pixel addressing). This file provides fb_cursor_tick() which
 * is called from sched_irq_tick (IRQ0) to blink the cursor.
 *
 * fb_init() is kept for API compatibility but printk_set_fb() is the
 * real initialiser; they share the same physical framebuffer.
 */

#include "ark/types.h"
#include "ark/fb.h"
#include "ark/printk.h"

/* Cursor blink: toggle every BLINK_TICKS IRQ0 ticks.
 * At 100 Hz → 25 ticks = 250ms half-period (2 Hz blink). */
#define BLINK_TICKS 25

static u32 g_tick = 0;

void fb_init(const ark_fb_info_t *info) {
    (void)info;  /* printk_set_fb() is the real init */
}

void fb_clear(void) {
    /* Delegated to printk layer which owns the shadow buffer */
}

void fb_putc(char c) {
    /* Delegated to printk */
    printk("%c", c);
}

/* Called from sched_irq_tick every IRQ0 fire */
void fb_cursor_tick(void) {
    if (++g_tick >= BLINK_TICKS) {
        g_tick = 0;
        printk_cursor_toggle();
    }
}
