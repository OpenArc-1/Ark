/**
 * include/ark/sched.h — Ark preemptive scheduler
 */
#pragma once
#include "ark/types.h"

#define SCHED_MAX_TASKS     64
#define SCHED_STACK_SIZE    (16 * 1024)
#define SCHED_NAME_LEN      32
#define SCHED_TIMESLICE_DEF 10

typedef enum {
    TASK_EMPTY   = 0,
    TASK_READY   = 1,
    TASK_RUNNING = 2,
    TASK_BLOCKED = 3,
    TASK_ZOMBIE  = 4,
} task_state_t;

#define PRIO_HIGH   0
#define PRIO_NORMAL 1
#define PRIO_LOW    2
#define PRIO_IDLE   3
#define PRIO_LEVELS 4

/* Minimal saved context — just the ESP.
 * The full register state lives on the task's kernel stack
 * as a pushal+iret frame. sched_irq_tick saves/restores ESP only. */
typedef struct { u32 esp; } task_ctx_t;

typedef struct task {
    u32          tid;
    char         name[SCHED_NAME_LEN];
    task_state_t state;
    u8           priority;

    task_ctx_t   ctx;            /* saved ESP — full frame on stack  */
    u8          *stack;
    u32          stack_size;

    u32          timeslice;
    u32          timeslice_reset;
    u64          total_ticks;
    u64          run_count;

    int          exit_code;
    u32          parent_tid;
    u32          cpu_affinity;
    u32          sleep_until;
    void        *user_data;
} task_t;

typedef struct {
    u32  total_tasks;
    u32  ready_tasks;
    u32  blocked_tasks;
    u32  zombie_tasks;
    u64  total_switches;
    u32  current_tid;
    u32  tick_count;
    u32  timeslice_ms;
} sched_stats_t;

/* ── API ──────────────────────────────────────────────────────────── */
void sched_init(void);
u32  sched_spawn(const char *name, void (*entry)(void *), void *arg,
                 u8 priority, u32 stack_size);

/* Called from irq0_handler asm with current ESP; returns new ESP */
u32  sched_irq_tick(u32 current_esp);

/* Legacy stub kept for compatibility */
void sched_tick(void);

void sched_yield(void);
void sched_sleep(u32 ticks);
void sched_exit(int code);   /* terminate calling task, become ZOMBIE */
void sched_kill(u32 tid);
void sched_kill_by_name(const char *name);
u32  sched_find_tid_by_name(const char *name);
int  sched_wait(u32 tid);

task_t *sched_current(void);
u32     sched_current_tid(void);
u32     sched_get_task_state(u32 tid);

void sched_get_stats(sched_stats_t *out);
void sched_list_tasks(void);
u32  sched_cpu_thread_count(void);

void sched_block(u32 tid);
void sched_unblock(u32 tid);
void sched_set_affinity(u32 tid, u32 cpu_mask);

extern volatile u32 g_sched_ticks;
extern u32 g_pit_hz;           /* actual PIT tick rate — set in sched_init */

/* ms-based sleep — converts to ticks using real g_pit_hz */
void sched_sleep_ms(u32 ms);
/* Returns the programmed PIT hz */
u32  sched_get_hz(void);
/* Milliseconds since scheduler started */
u32  sched_uptime_ms(void);
