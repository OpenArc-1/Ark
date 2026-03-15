/**
 * gen/sched.c — Ark preemptive scheduler (x86 32-bit)
 *
 * Context switch design (the only correct way for x86 IRQ-driven preemption):
 *
 *   IRQ0 fires → CPU pushes EIP/CS/EFLAGS onto current stack (iret frame)
 *   irq0_handler (asm) pushes all GP registers (pushal → 8×u32 = 32 bytes)
 *   irq0_handler calls  sched_irq_tick(esp) passing current ESP
 *   sched_irq_tick()    updates ticks, decides if we switch
 *   if switching:       saves esp into cur->ctx.esp
 *                       sets g_current_idx to next task
 *                       returns next->ctx.esp
 *   irq0_handler        sets ESP = return value from sched_irq_tick
 *                       popal + iret → resumes new task transparently
 *
 * For the very first time a spawned task runs:
 *   Its ctx.esp points to a fake frame:
 *     [esp+0..28] = zero pushal registers
 *     [esp+32]    = entry function address  (fake EIP)
 *     [esp+36]    = 0x08 (kernel CS)
 *     [esp+40]    = 0x202 (EFLAGS with IF=1)
 *   When irq0_handler does popal+iret it jumps straight into entry().
 *
 * This means sched_switch_to() is NEVER called from C — no C stack
 * corruption is possible.
 */

#include "ark/types.h"
#include "ark/sched.h"
#include "ark/printk.h"
#include "ark/kconfig.h"

/* ── PIT — single source of truth from kconfig ───────────────────── */
/* PIT_BASE_FREQ is the fixed 8254 crystal: exactly 1.193182 MHz.
 * CONFIG_SCHED_PIT_HZ drives the tick rate (kconfig, default 100).
 * Timeslice = CONFIG_SCHED_TIMESLICE_MS converted to ticks at runtime.
 * All timing — sleep, timestamps, cursor blink — uses g_pit_hz so
 * the only thing to change is kconfig.                              */
#define PIT_BASE_FREQ  1193182UL

/* Timeslice in ticks derived from ms + hz, rounded up */
#define PIT_TIMESLICE_TICKS(hz)     (((u32)(CONFIG_SCHED_TIMESLICE_MS) * (hz) + 999u) / 1000u)

/* Exported: runtime hz after calibration */
u32 g_pit_hz = CONFIG_SCHED_PIT_HZ;

/* Defined in arch/x86_64/pit.S — programs the 8254 */
extern void pit_init(u32 hz);

/* ── CPUID ───────────────────────────────────────────────────────── */
static void do_cpuid(u32 leaf,u32 sub,u32*a,u32*b,u32*c,u32*d){
    __asm__ __volatile__("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(sub));
}
u32 sched_cpu_thread_count(void) {
    u32 a,b,c,d;
    do_cpuid(0,0,&a,&b,&c,&d);
    u32 max=a;
    if(max<1) return 1;
    do_cpuid(1,0,&a,&b,&c,&d);
    u32 logical=(b>>16)&0xFF; if(!logical) logical=1;
    if(max>=0xB){ do_cpuid(0xB,0,&a,&b,&c,&d); u32 t=b&0xFFFF; if(t) return t; }
    return logical;
}

/* ── iret-frame layout pushed by irq0_handler ────────────────────
 * After pushal the stack looks like (low addr at top):
 *   esp+ 0  EDI  \
 *   esp+ 4  ESI   |
 *   esp+ 8  EBP   |  pushal (32 bytes)
 *   esp+12  ESP*  |  (* saved ESP before pushal, we ignore it)
 *   esp+16  EBX   |
 *   esp+20  EDX   |
 *   esp+24  ECX   |
 *   esp+28  EAX  /
 *   esp+32  EIP  \
 *   esp+36  CS    |  iret frame pushed by CPU
 *   esp+40  EFLAGS/
 */
#define IRET_FRAME_BYTES  (8*4 + 3*4)   /* pushal(32) + iret(12) = 44 */

/* ── Global state ────────────────────────────────────────────────── */
volatile u32 g_sched_ticks = 0;

static task_t  g_tasks[SCHED_MAX_TASKS];
static u32     g_current_idx  = 0;   /* index into g_tasks[] */
static u32     g_next_tid     = 1;
static bool    g_sched_ready  = false;
static u64     g_total_switches = 0;
static u32     g_timeslice_cfg  = 10; /* recalculated in sched_init from hz */

/* ── Static per-task stacks ──────────────────────────────────────── */
/* One stack slot per possible task — previously only 8 were available,
 * which exhausted immediately when tasks didn't free their stacks on exit. */
#define STATIC_STACKS SCHED_MAX_TASKS
static u8  g_static_stacks[STATIC_STACKS][SCHED_STACK_SIZE];
static u8  g_stack_used[STATIC_STACKS];

static u8 *alloc_stack(void) {
    for (int i=0;i<STATIC_STACKS;i++) {
        if (!g_stack_used[i]) {
            g_stack_used[i]=1;
            for(u32 j=0;j<SCHED_STACK_SIZE;j++) g_static_stacks[i][j]=0;
            return g_static_stacks[i];
        }
    }
    return NULL;
}
static void free_stack(u8 *s) {
    for(int i=0;i<STATIC_STACKS;i++)
        if(g_static_stacks[i]==s){ g_stack_used[i]=0; return; }
}

static void _strncpy(char *d,const char *s,u32 n){
    u32 i; for(i=0;i<n-1&&s[i];i++) d[i]=s[i]; d[i]='\0';
}

/* Forward declaration — sched_exit is defined later but sched_spawn plants
 * its address as the task return address so tasks clean up on normal exit. */
void sched_exit(int code);

/* ── Pick next runnable task ─────────────────────────────────────── */
static u32 sched_pick_next(void) {
    for (int pri=PRIO_HIGH; pri<=PRIO_IDLE; pri++) {
        for (u32 i=1; i<SCHED_MAX_TASKS; i++) {
            u32 idx = (g_current_idx + i) % SCHED_MAX_TASKS;
            task_t *t = &g_tasks[idx];
            if (t->state==TASK_READY && (int)t->priority==pri)
                return idx;
        }
    }
    return g_current_idx; /* stay */
}

/* ── Wake sleepers ───────────────────────────────────────────────── */
static void sched_wake_sleepers(void) {
    for(int i=0;i<SCHED_MAX_TASKS;i++){
        task_t *t=&g_tasks[i];
        if(t->state==TASK_BLOCKED && t->sleep_until &&
           g_sched_ticks >= t->sleep_until){
            t->sleep_until=0; t->state=TASK_READY;
        }
    }
}

/* ── sched_init ──────────────────────────────────────────────────── */
void sched_init(void) {
    for(int i=0;i<SCHED_MAX_TASKS;i++){
        g_tasks[i].state=TASK_EMPTY; g_tasks[i].tid=0;
    }
    for(int i=0;i<STATIC_STACKS;i++) g_stack_used[i]=0;

    /* Slot 1 = kernel_main (current boot thread).
     * We don't set ctx.esp here — it will be filled by the first
     * call to sched_irq_tick() when IRQ0 fires.                    */
    task_t *km = &g_tasks[1];
    km->tid             = g_next_tid++;
    km->state           = TASK_RUNNING;
    km->priority        = PRIO_NORMAL;
    km->timeslice       = g_timeslice_cfg;
    km->timeslice_reset = g_timeslice_cfg;
    km->cpu_affinity    = 0xFFFFFFFF;
    _strncpy(km->name, "kernel_main", SCHED_NAME_LEN);
    g_current_idx = 1;

    u32 threads = sched_cpu_thread_count();

    /* Program PIT at the kconfig rate. The assembly pit_init uses the
     * real 8254 base frequency (1193182 Hz) so divisor is exact.
     * Measure the actual CPU TSC rate right after so printk timestamps
     * and sleep_ms are accurate on real hardware regardless of CPU speed. */
    u32 hz = CONFIG_SCHED_PIT_HZ;
    pit_init(hz);
    g_pit_hz = hz;

    /* Recompute timeslice now that hz is known */
    g_timeslice_cfg = PIT_TIMESLICE_TICKS(hz);
    if (g_timeslice_cfg == 0) g_timeslice_cfg = 1;

    /* Re-apply to kernel_main task already inserted above */
    g_tasks[1].timeslice       = g_timeslice_cfg;
    g_tasks[1].timeslice_reset = g_timeslice_cfg;

    /* Tell printk the real tick rate so timestamps are correct */
    { extern void printk_set_hz(u32); printk_set_hz(hz); }

    /* Calibrate TSC against the PIT so sleep_ms is cycle-accurate */
    { extern void tsc_calibrate(void); tsc_calibrate(); }

    g_sched_ready = true;
    printk(T,"sched: %u Hz  timeslice=%u ticks (%u ms)  %u thread(s)\n",
           hz, g_timeslice_cfg, CONFIG_SCHED_TIMESLICE_MS, threads);
}

/* ── sched_spawn ─────────────────────────────────────────────────── */
u32 sched_spawn(const char *name, void (*entry)(void *), void *arg,
                u8 priority, u32 stack_size) {
    (void)stack_size;

    int slot=-1;
    for(int i=2;i<SCHED_MAX_TASKS;i++)
        if(g_tasks[i].state==TASK_EMPTY){ slot=i; break; }
    if(slot<0){ printk(T,"sched: task table full\n"); return 0; }

    u8 *stack = alloc_stack();
    if(!stack){ printk(T,"sched: no stack memory\n"); return 0; }

    task_t *t = &g_tasks[slot];
    t->tid             = g_next_tid++;
    t->state           = TASK_READY;
    t->priority        = priority<PRIO_LEVELS ? priority : PRIO_NORMAL;
    t->timeslice       = g_timeslice_cfg;
    t->timeslice_reset = g_timeslice_cfg;
    t->total_ticks     = 0;
    t->run_count       = 0;
    t->exit_code       = 0;
    t->parent_tid      = sched_current_tid();
    t->cpu_affinity    = 0xFFFFFFFF;
    t->sleep_until     = 0;
    t->stack           = stack;
    t->stack_size      = SCHED_STACK_SIZE;
    t->user_data       = arg;
    _strncpy(t->name, name?name:"task", SCHED_NAME_LEN);

    /*
     * Build a fake IRQ frame at the top of the stack.
     * irq0_handler will do popal + iret using this frame,
     * which will "return" into entry(arg).
     *
     * Stack layout (high address at top, stack grows down):
     *   [top - 4]  EFLAGS = 0x202   (IF=1)
     *   [top - 8]  CS     = 0x08    (kernel code segment)
     *   [top -12]  EIP    = entry   (where to jump)
     *   [top -16]  EAX = 0          \
     *   [top -20]  ECX = 0           |
     *   [top -24]  EDX = 0           |
     *   [top -28]  EBX = 0           | pushal registers
     *   [top -32]  ESP = 0 (ignored) |
     *   [top -36]  EBP = 0           |
     *   [top -40]  ESI = 0           |
     *   [top -44]  EDI = arg        /  ← EDI = first arg via convention
     *
     * NOTE: x86 C calling convention passes args on stack, not in regs.
     * entry is called via iret not call, so we put arg below the fake
     * return address instead.  We use a trampoline approach:
     * We put entry as EIP and put a minimal stack so entry(arg) works.
     */

    /*
     * Build initial stack frame for irq0_handler to restore.
     *
     * irq0_handler does:  pushal → call sched_irq_tick → movl %eax,%esp
     *                     → popal → iret
     *
     * So ctx.esp must point to a pushal-shaped region followed by an
     * iret frame.  After popal+iret, ESP lands on whatever is above
     * the iret frame — that becomes entry()'s initial stack, where we
     * plant [ret_addr, arg] so entry() can call-return normally.
     *
     * High address (stack bottom)
     *   [ctx.esp + 52]  arg             <- entry sees at [esp+4]
     *   [ctx.esp + 48]  sched_yield     <- entry sees at [esp+0] (ret addr)
     *   [ctx.esp + 44]  EFLAGS = 0x202  \
     *   [ctx.esp + 40]  CS     = 0x08    > iret frame (12 bytes)
     *   [ctx.esp + 36]  EIP    = entry  /   after iret: ESP = ctx.esp+48
     *   [ctx.esp + 32]  EAX = 0        \
     *   [ctx.esp + 28]  ECX = 0         |
     *   [ctx.esp + 24]  EDX = 0         |
     *   [ctx.esp + 20]  EBX = 0         > pushal frame (32 bytes)
     *   [ctx.esp + 16]  ESP = 0 (skip)  |
     *   [ctx.esp + 12]  EBP = 0         |
     *   [ctx.esp +  8]  ESI = 0         |
     *   [ctx.esp +  4]  EDI = 0         |  <- popal pops EDI first
     * ctx.esp ──────────────────────────'
     * Low address (stack top)
     *
     * NOTE: pushal pushes EAX first → EDI last (EDI = lowest addr).
     *       popal  pops  EDI first → EAX last.
     *       We push in reverse order (building down) so memory layout matches.
     */
    u32 *sp = (u32 *)(stack + SCHED_STACK_SIZE);

    /* call frame above iret — entry() sees these on its stack */
    *(--sp) = (u32)arg;           /* [esp+4]: argument                  */
    /* Use sched_exit(0) as the return address so that when entry() returns
     * normally, the task marks itself ZOMBIE and frees its stack slot.
     * Previously this was sched_yield, which looped forever and leaked
     * the stack — exhausting all 8 static stack slots after a few spawns. */
    *(--sp) = (u32)sched_exit;    /* [esp+0]: return address — clean exit */

    /* iret frame */
    *(--sp) = 0x00000202u;        /* EFLAGS: IF=1                       */
    *(--sp) = 0x00000008u;        /* CS: kernel code segment            */
    *(--sp) = (u32)entry;         /* EIP: jump here on first iret       */

    /* pushal frame — push EAX first (highest addr) → EDI last (lowest) */
    *(--sp) = 0;  /* EAX */
    *(--sp) = 0;  /* ECX */
    *(--sp) = 0;  /* EDX */
    *(--sp) = 0;  /* EBX */
    *(--sp) = 0;  /* ESP slot (popal skips) */
    *(--sp) = 0;  /* EBP */
    *(--sp) = 0;  /* ESI */
    *(--sp) = 0;  /* EDI — lowest address; popal pops this first */

    t->ctx.esp = (u32)sp;         /* hand this to irq0_handler          */

    printk(T, "sched: spawned '%s' tid=%u pri=%u\n",
           t->name, t->tid, (u32)t->priority);
    return t->tid;
}

/* ── sched_irq_tick — called from IRQ0 asm handler ─────────────────
 * Receives the current ESP (pointing to pushal frame).
 * Returns the ESP to use (same task or new task).
 * THIS IS THE ONLY PLACE context switches happen.
 */
u32 sched_irq_tick(u32 current_esp) {
    if (!g_sched_ready) return current_esp;

    g_sched_ticks++;
    sched_wake_sleepers();

    /* Blink the framebuffer cursor on every timer tick */
    { extern void fb_cursor_tick(void); fb_cursor_tick(); }

    /* Real-hardware anti-blank: re-assert VGA unblank every ~30 seconds.
     * Some BIOS/UEFI firmware re-enables the VGA Sequencer screen-off bit
     * after ~1 minute of "inactivity" (no BIOS video calls).  Linux fixes
     * this in fbcon via console_callback/unblank_screen; we do the same.
     * At CONFIG_SCHED_PIT_HZ=100, 3000 ticks = 30 seconds. */
    if ((g_sched_ticks % 3000u) == 0u) {
        extern void display_vga_unblank(void);
        display_vga_unblank();
    }

    task_t *cur = &g_tasks[g_current_idx];
    cur->ctx.esp = current_esp;   /* save current task's stack pointer */
    cur->total_ticks++;

    if (cur->timeslice > 0) cur->timeslice--;

    if (cur->timeslice == 0) {
        cur->timeslice = cur->timeslice_reset;

        u32 next_idx = sched_pick_next();
        if (next_idx != g_current_idx) {
            /* commit the switch */
            if (cur->state == TASK_RUNNING)
                cur->state = TASK_READY;

            task_t *next = &g_tasks[next_idx];
            next->state = TASK_RUNNING;
            next->run_count++;
            g_current_idx = next_idx;
            g_total_switches++;

            return next->ctx.esp;   /* asm handler will set ESP here */
        }
    }

    return cur->ctx.esp;   /* no switch — same task */
}

/* Legacy sched_tick kept for anything that calls it directly */
void sched_tick(void) { /* no-op: work done in sched_irq_tick */ }

/* ── sched_get_hz — return actual PIT tick rate ─────────────────── */
u32 sched_get_hz(void) { return g_pit_hz; }

/* ── sched_sleep_ms — sleep for milliseconds not ticks ──────────── */
void sched_sleep_ms(u32 ms) {
    if (!ms) return;
    /* Convert ms to ticks using real hz. Add 1 to guarantee at least
     * one full tick elapses even for small ms values.               */
    u32 ticks = (ms * g_pit_hz + 999u) / 1000u;
    if (!ticks) ticks = 1;
    sched_sleep(ticks);
}

/* ── sched_uptime_ms — milliseconds since scheduler started ─────── */
u32 sched_uptime_ms(void) {
    if (!g_pit_hz) return 0;
    return (g_sched_ticks * 1000u) / g_pit_hz;
}

/* ── sched_yield ─────────────────────────────────────────────────── */
void sched_yield(void) {
    if (!g_sched_ready) return;
    /* Enable interrupts and halt until next IRQ fires (IRQ0 will switch us) */
    __asm__ __volatile__("sti\n\thlt\n\tcli");
}

/* ── sched_sleep ─────────────────────────────────────────────────── */
void sched_sleep(u32 ticks) {
    if (!g_sched_ready || !ticks) return;
    __asm__ __volatile__("cli");
    g_tasks[g_current_idx].state       = TASK_BLOCKED;
    g_tasks[g_current_idx].sleep_until = g_sched_ticks + ticks;
    __asm__ __volatile__("sti");
    /* Spin-wait: IRQ0 wakes us by setting state back to READY/RUNNING */
    while (g_tasks[g_current_idx].state == TASK_BLOCKED)
        __asm__ __volatile__("sti\n\thlt\n\tcli");
    __asm__ __volatile__("sti");
}
/* ── sched_block / unblock ───────────────────────────────────────── */
void sched_block(u32 tid) {
    for(int i=0;i<SCHED_MAX_TASKS;i++)
        if(g_tasks[i].tid==tid && g_tasks[i].state!=TASK_EMPTY){
            g_tasks[i].state=TASK_BLOCKED; return; }
}
void sched_unblock(u32 tid) {
    for(int i=0;i<SCHED_MAX_TASKS;i++)
        if(g_tasks[i].tid==tid && g_tasks[i].state==TASK_BLOCKED){
            g_tasks[i].state=TASK_READY; g_tasks[i].sleep_until=0; return; }
}

/* ── sched_exit — called by a task to terminate itself ───────────── */
void sched_exit(int code) {
    __asm__ __volatile__("cli");
    task_t *t = &g_tasks[g_current_idx];
    t->exit_code = code;
    t->state     = TASK_ZOMBIE;
    if (t->stack) { free_stack(t->stack); t->stack = NULL; }
    __asm__ __volatile__("sti");
    /* Yield forever — sched_irq_tick will never pick a ZOMBIE task */
    for (;;) __asm__ __volatile__("sti\n\thlt\n\tcli");
}

/* ── sched_kill ──────────────────────────────────────────────────── */
void sched_kill(u32 tid) {
    for(int i=1;i<SCHED_MAX_TASKS;i++){
        task_t *t=&g_tasks[i];
        if(t->tid==tid){
            t->state=TASK_ZOMBIE; t->exit_code=-1;
            if(t->stack){ free_stack(t->stack); t->stack=NULL; }
            printk(T,"sched: killed task[%u] '%s'\n",tid,t->name);
            return;
        }
    }
}

/* ── sched_find_tid_by_name ──────────────────────────────────────── */
u32 sched_find_tid_by_name(const char *name) {
    if(!name) return 0;
    /* exact match first */
    for(int i=0;i<SCHED_MAX_TASKS;i++){
        task_t *t=&g_tasks[i];
        if(t->state==TASK_EMPTY) continue;
        int j=0;
        while(name[j]&&t->name[j]&&name[j]==t->name[j]) j++;
        if(!name[j]&&!t->name[j]) return t->tid;
    }
    /* prefix/substring match */
    for(int i=0;i<SCHED_MAX_TASKS;i++){
        task_t *t=&g_tasks[i];
        if(t->state==TASK_EMPTY) continue;
        for(int ni=0;t->name[ni];ni++){
            int j=0;
            while(name[j]&&t->name[ni+j]==name[j]) j++;
            if(!name[j]) return t->tid;
        }
    }
    return 0;
}
void sched_kill_by_name(const char *name){
    u32 tid=sched_find_tid_by_name(name);
    if(tid) sched_kill(tid);
    else printk(T,"sched: no task '%s'\n",name);
}

/* ── sched_set_affinity ──────────────────────────────────────────── */
void sched_set_affinity(u32 tid,u32 mask){
    for(int i=0;i<SCHED_MAX_TASKS;i++)
        if(g_tasks[i].tid==tid&&g_tasks[i].state!=TASK_EMPTY){
            g_tasks[i].cpu_affinity=mask; return; }
}

/* ── sched_wait ──────────────────────────────────────────────────── */
int sched_wait(u32 tid){
    while(1){
        for(int i=0;i<SCHED_MAX_TASKS;i++){
            task_t *t=&g_tasks[i];
            if(t->tid==tid){
                if(t->state==TASK_ZOMBIE){
                    int c=t->exit_code; t->state=TASK_EMPTY; t->tid=0; return c;
                }
                goto kw;
            }
        }
        return -1;
kw:     sched_yield();
    }
}

task_t *sched_current(void){ return &g_tasks[g_current_idx]; }
u32 sched_current_tid(void){ return g_sched_ready?g_tasks[g_current_idx].tid:0; }

/* Returns the raw task state for a tid, or 0 if not found / already empty */
u32 sched_get_task_state(u32 tid) {
    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        if (g_tasks[i].tid == tid)
            return (u32)g_tasks[i].state;
    }
    return 0;   /* not found = task slot is TASK_EMPTY */
}

void sched_get_stats(sched_stats_t *out){
    out->total_tasks=out->ready_tasks=out->blocked_tasks=out->zombie_tasks=0;
    out->total_switches=g_total_switches;
    out->current_tid=sched_current_tid();
    out->tick_count=g_sched_ticks;
    out->timeslice_ms=g_timeslice_cfg;
    for(int i=0;i<SCHED_MAX_TASKS;i++){
        task_t *t=&g_tasks[i];
        if(t->state==TASK_EMPTY) continue;
        out->total_tasks++;
        if(t->state==TASK_READY||t->state==TASK_RUNNING) out->ready_tasks++;
        else if(t->state==TASK_BLOCKED) out->blocked_tasks++;
        else if(t->state==TASK_ZOMBIE)  out->zombie_tasks++;
    }
}

static const char *state_name(task_state_t s){
    switch(s){
    case TASK_EMPTY:   return "empty  ";
    case TASK_READY:   return "ready  ";
    case TASK_RUNNING: return "running";
    case TASK_BLOCKED: return "blocked";
    case TASK_ZOMBIE:  return "zombie ";
    default:           return "???    ";
    }
}
static const char *prio_name(u8 p){
    switch(p){
    case PRIO_HIGH:   return "high  ";
    case PRIO_NORMAL: return "normal";
    case PRIO_LOW:    return "low   ";
    case PRIO_IDLE:   return "idle  ";
    default:          return "?     ";
    }
}

void sched_list_tasks(void){
    sched_stats_t st; sched_get_stats(&st);
    u32 threads=sched_cpu_thread_count();
    printk(T,"\n=== Ark Task List ===\n");
    printk(T,"CPU threads: %u  |  tick: %u  |  switches: %u\n",
           threads,st.tick_count,(u32)st.total_switches);
    printk(T,"Tasks: %u total  %u ready  %u blocked  %u zombie\n\n",
           st.total_tasks,st.ready_tasks,st.blocked_tasks,st.zombie_tasks);
    printk(T," TID  %-20s  STATE    PRIO    AFFIN  TICKS      RUNS\n","NAME");
    printk(T," ---  --------------------  -------  ------  -----  ---------  ----\n");
    for(int i=0;i<SCHED_MAX_TASKS;i++){
        task_t *t=&g_tasks[i];
        if(t->state==TASK_EMPTY) continue;
        char aff[8]; u32 m=t->cpu_affinity;
        if(!m||m==0xFFFFFFFF){ aff[0]='a';aff[1]='l';aff[2]='l';aff[3]=0; }
        else if(!(m&(m-1))){
            u32 core=0,tmp=m; while(tmp>1){tmp>>=1;core++;}
            aff[0]='c';aff[1]='p';aff[2]='u';
            aff[3]=(char)('0'+(core<9?core:9));aff[4]=0;
        } else {
            const char *h="0123456789abcdef";
            aff[0]='0';aff[1]='x';aff[2]=h[(m>>4)&0xF];aff[3]=h[m&0xF];aff[4]=0;
        }
        char mk=((u32)i==g_current_idx)?'*':' ';
        printk(T,"%c%3u  %-20s  %s  %s  %-5s  %9u  %4u\n",
               mk,t->tid,t->name,state_name(t->state),prio_name(t->priority),
               aff,(u32)t->total_ticks,(u32)t->run_count);
    }
    printk(T,"\n* = currently running\n\n");
}