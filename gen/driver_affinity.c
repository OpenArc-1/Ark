/**
 * gen/driver_affinity.c — CPU-affinity-based driver task launcher
 *
 * After the scheduler and PCI/USB subsystems are up, this module
 * spawns background kernel tasks for each driver class and pins
 * them to specific CPU cores:
 *
 *   Core 0  — kernel_main (boot thread), default
 *   Core 1  — USB subsystem poll task  (USB_AFFINITY = 0x02)
 *   Core 2  — PCI/ETH driver task      (ETH_AFFINITY = 0x04)
 *   Core 3  — ATA scan task            (ATA_AFFINITY = 0x08)
 *   Core 4  — SATA scan task           (SATA_AFFINITY = 0x10)
 *
 * On single-core machines every task uses CPU 0 (mask 0x1).
 * ATA and SATA scans are one-shot: they run once, store results,
 * then call sched_exit(0).  driver_affinity_wait_storage() blocks
 * kernel_main until both scans complete.
 *
 * The "poll" tasks (eth, usb) are cooperative — they call
 * sched_sleep(1) after each work unit so they don't starve others.
 */

#include "ark/types.h"
#include "ark/sched.h"
#include "ark/printk.h"
#include "ark/kconfig.h"

/* ── Forward declarations for driver poll entry points ───────────── */
__attribute__((weak)) void e1000_poll(void)  {}
__attribute__((weak)) void rtl8139_poll(void){}
__attribute__((weak)) void usb_poll(void)    {}
__attribute__((weak)) void usb_hid_poll(void){}

/* ── Forward declarations for storage init ───────────────────────── */
#if CONFIG_ATA_ENABLE
extern void ata_init(void);
#endif
#if CONFIG_SATA_ENABLE
extern void sata_init(void);
#endif
extern int  disk_load_init(void);

/* ── Affinity masks ──────────────────────────────────────────────── */
#define CORE0_MASK  0x01u
#define CORE1_MASK  0x02u
#define CORE2_MASK  0x04u
#define CORE3_MASK  0x08u
#define CORE4_MASK  0x10u

/* ── Storage scan completion tracking ───────────────────────────── */
static volatile u32 g_ata_tid  = 0;
static volatile u32 g_sata_tid = 0;

/* ── Driver poll loops ───────────────────────────────────────────── */

static void task_eth_poll(void *arg) {
    (void)arg;
    printk(T, "drv_affinity: eth poll task started (tid %u)\n",
           sched_current_tid());
    for (;;) {
#if CONFIG_E1000_ENABLE
        e1000_poll();
#endif
#if CONFIG_NET_ENABLE
        rtl8139_poll();
#endif
        sched_sleep(1);   /* yield every tick (~10ms) */
    }
}

static void task_usb_poll(void *arg) {
    (void)arg;
    printk(T, "drv_affinity: usb poll task started (tid %u)\n",
           sched_current_tid());
    for (;;) {
#if CONFIG_USB_ENABLE
        usb_poll();
#endif
#if CONFIG_USB_HID
        usb_hid_poll();
#endif
        sched_sleep(1);
    }
}

/* ── One-shot ATA scan task ──────────────────────────────────────── */
static void task_ata_scan(void *arg) {
    (void)arg;
    printk(T, "drv_affinity: ata_scan started (tid %u)\n",
           sched_current_tid());
#if CONFIG_ATA_ENABLE
    ata_init();
#endif
    printk(T, "drv_affinity: ata_scan done\n");
    sched_exit(0);
}

/* ── One-shot SATA scan task ─────────────────────────────────────── */
static void task_sata_scan(void *arg) {
    (void)arg;
    printk(T, "drv_affinity: sata_scan started (tid %u)\n",
           sched_current_tid());
#if CONFIG_SATA_ENABLE
    sata_init();
#endif
    printk(T, "drv_affinity: sata_scan done\n");
    sched_exit(0);
}

/* ── Public init ─────────────────────────────────────────────────── */
void driver_affinity_init(void) {
    u32 threads = sched_cpu_thread_count();

    printk(T, "drv_affinity: %u CPU thread(s) — assigning driver tasks\n",
           threads);

    /* Determine affinity per class based on thread count */
    u32 eth_affinity  = CORE0_MASK;
    u32 usb_affinity  = CORE0_MASK;
    u32 ata_affinity  = CORE0_MASK;
    u32 sata_affinity = CORE0_MASK;

    if (threads >= 2) usb_affinity  = CORE1_MASK;
    if (threads >= 3) eth_affinity  = CORE2_MASK;
    if (threads >= 4) ata_affinity  = CORE3_MASK;
    if (threads >= 5) sata_affinity = CORE4_MASK;

    /* ETH poll */
#if CONFIG_NET_ENABLE || CONFIG_E1000_ENABLE
    u32 eth_tid = sched_spawn("eth_poll", task_eth_poll, NULL,
                              PRIO_NORMAL, 0);
    if (eth_tid) {
        sched_set_affinity(eth_tid, eth_affinity);
        printk(T, "drv_affinity: eth_poll tid=%u affinity=0x%x\n",
               eth_tid, eth_affinity);
    }
#endif

    /* USB poll */
#if CONFIG_USB_ENABLE
    u32 usb_tid = sched_spawn("usb_poll", task_usb_poll, NULL,
                              PRIO_NORMAL, 0);
    if (usb_tid) {
        sched_set_affinity(usb_tid, usb_affinity);
        printk(T, "drv_affinity: usb_poll tid=%u affinity=0x%x\n",
               usb_tid, usb_affinity);
    }
#endif

    /* ATA scan — one-shot */
    {
        u32 tid = sched_spawn("ata_scan", task_ata_scan, NULL,
                              PRIO_HIGH, 0);
        if (tid) {
            sched_set_affinity(tid, ata_affinity);
            g_ata_tid = tid;
            printk(T, "drv_affinity: ata_scan  tid=%u affinity=0x%x\n",
                   tid, ata_affinity);
        }
    }

    /* SATA scan — one-shot */
    {
        u32 tid = sched_spawn("sata_scan", task_sata_scan, NULL,
                              PRIO_HIGH, 0);
        if (tid) {
            sched_set_affinity(tid, sata_affinity);
            g_sata_tid = tid;
            printk(T, "drv_affinity: sata_scan tid=%u affinity=0x%x\n",
                   tid, sata_affinity);
        }
    }

    printk(T, "drv_affinity: core layout:\n");
    printk(T, "  core 0: kernel_main\n");
    if (threads >= 2)
        printk(T, "  core 1: usb_poll\n");
    if (threads >= 3)
        printk(T, "  core 2: eth_poll\n");
    if (threads >= 4)
        printk(T, "  core 3: ata_scan\n");
    else
        printk(T, "  core 0: ata_scan (shared, <4 threads)\n");
    if (threads >= 5)
        printk(T, "  core 4: sata_scan\n");
    else
        printk(T, "  core 0: sata_scan (shared, <5 threads)\n");
}

/**
 * driver_affinity_wait_storage — block until ata_scan and sata_scan finish.
 *
 * On x86_64 the scheduler's sched_irq_tick() is a 32-bit context switcher
 * that is NOT called from the 64-bit IRQ0 handler, so spawned tasks never
 * receive CPU time.  sched_wait() would loop on sched_yield() forever.
 *
 * Detection: if sched_cpu_thread_count() == 1, or if after a short busy-wait
 * the tasks are still TASK_READY (never ran), fall back to running the storage
 * inits inline in the current context before returning.
 */
void driver_affinity_wait_storage(void) {
    extern u32 sched_get_task_state(u32 tid);

    /* Give spawned tasks a chance to run: yield a few hundred ticks.
     * On a working SMP/preemptive scheduler they will finish quickly.
     * On x86_64 single-core with no IRQ0→sched wiring they stay READY. */
    u32 waited = 0;
    const u32 WAIT_LIMIT = 500;   /* ~500 yield cycles ≈ 5 seconds */

    if (g_ata_tid) {
        printk(T, "drv_affinity: waiting for ata_scan (tid %u)...\n",
               g_ata_tid);
        while (waited < WAIT_LIMIT) {
            u32 st = sched_get_task_state(g_ata_tid);
            if (st == 0 /* TASK_EMPTY = done/reaped */ ||
                st == 3 /* TASK_ZOMBIE */) {
                if (st == 3) { sched_wait(g_ata_tid); }
                break;
            }
            sched_yield();
            waited++;
        }
        if (waited >= WAIT_LIMIT) {
            /* Tasks never ran — scheduler not driving them.
             * Run ata_init inline so boot can continue. */
            printk(T, "drv_affinity: ata_scan stalled — running inline\n");
#if CONFIG_ATA_ENABLE
            { extern void ata_init(void); ata_init(); }
#endif
        }
        g_ata_tid = 0;
    }

    waited = 0;
    if (g_sata_tid) {
        printk(T, "drv_affinity: waiting for sata_scan (tid %u)...\n",
               g_sata_tid);
        while (waited < WAIT_LIMIT) {
            u32 st = sched_get_task_state(g_sata_tid);
            if (st == 0 || st == 3) {
                if (st == 3) { sched_wait(g_sata_tid); }
                break;
            }
            sched_yield();
            waited++;
        }
        if (waited >= WAIT_LIMIT) {
            printk(T, "drv_affinity: sata_scan stalled — running inline\n");
#if CONFIG_SATA_ENABLE
            { extern void sata_init(void); sata_init(); }
#endif
        }
        g_sata_tid = 0;
    }

    printk(T, "drv_affinity: storage scan complete\n");

    /* ── Init AFS and probe disks now that storage is ready ── */
#if CONFIG_AFS_ENABLE
    { extern void afs_init(void); afs_init(); }
#endif
    disk_load_init();
}
