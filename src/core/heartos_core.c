/* SPDX-License-Identifier: Apache-2.0 */
#include "heartos.h"
#include "heartos_cfg.h"
#include "heartos_time.h"
#include "heartos_port_int.h"
#include <string.h>

/* Minimal TCB */
typedef struct {
    uint32_t *sp; /* saved stack pointer (arch-defined) */
    uint32_t *stack_base;
    size_t stack_words;
    hrt_task_fn entry;
    void *arg;
    uint32_t wake_tick;
    uint16_t timeslice_cfg;
    uint16_t slice_left;
    uint8_t prio;
    uint8_t state;
} hrt_tcb_t;

/* Globals */
static hrt_tcb_t g_tcbs[HEARTOS_MAX_TASKS];
static int g_current = -1;
static uint32_t g_tick = 0;
static uint32_t g_tick_hz = 1000;
static hrt_policy_t g_policy = HRT_SCHED_PRIORITY_RR;
static uint16_t g_default_slice = 5;
static uint32_t g_core_hz = 0;
static hrt_tick_source_t g_tick_src = HRT_TICK_SYSTICK;


/* Ready queues per priority (store task indices) */
typedef struct {
    uint8_t q[HEARTOS_MAX_TASKS];
    uint8_t head, tail, count;
} prio_q_t;

static prio_q_t g_rq[HEARTOS_MAX_PRIO];

/* Forward decls used by other core files */
int hrt__pick_next_ready(void);

void hrt__make_ready(int id);

/* Provided by the port */
void hrt_port_enter_scheduler(void);

void hrt_port_yield_to_scheduler(void);

/* ------------- Queue helpers ------------- */
static void rq_push(uint8_t p, uint8_t id) {
    prio_q_t *q = &g_rq[p];
    if (q->count >= HEARTOS_MAX_TASKS) return;
    q->q[q->tail] = id;
    q->tail = (uint8_t) ((q->tail + 1) % HEARTOS_MAX_TASKS);
    q->count++;
}

static int rq_pop(uint8_t p) {
    prio_q_t *q = &g_rq[p];
    if (!q->count) return -1;
    int id = q->q[q->head];
    q->head = (uint8_t) ((q->head + 1) % HEARTOS_MAX_TASKS);
    q->count--;
    return id;
}

/* ------------- Core API ------------- */
int hrt_init(const hrt_config_t *cfg) {
    memset(g_tcbs, 0, sizeof(g_tcbs));
    memset(g_rq, 0, sizeof(g_rq));
    for (int i = 0; i < HEARTOS_MAX_TASKS; ++i) g_tcbs[i].state = HRT_UNUSED;

    g_tick = 0;
    g_current = -1;

    if (cfg) {
        g_tick_hz = cfg->tick_hz ? cfg->tick_hz : 1000;
        g_policy = cfg->policy;
        g_default_slice = cfg->default_slice ? cfg->default_slice : 5;
        g_core_hz = cfg->core_hz; // 0 if unknown
        g_tick_src = cfg->tick_src; // default if struct was zeroed is 0 => SYSTICK
    } else {
        g_tick_hz = 1000;
        g_policy = HRT_SCHED_PRIORITY_RR;
        g_default_slice = 5;
    }

    hrt_port_start_systick(g_tick_hz);
    return 0;
}

int hrt_create_task(hrt_task_fn fn, void *arg,
                    uint32_t *stack_words, size_t n_words,
                    const hrt_task_attr_t *attr) {
    if (!fn || !stack_words || n_words < 64) return -1;

    int id = -1;
    for (int i = 0; i < HEARTOS_MAX_TASKS; ++i) {
        if (g_tcbs[i].state == HRT_UNUSED) {
            id = i;
            break;
        }
    }
    if (id < 0) return -1;

    hrt_tcb_t *t = &g_tcbs[id];
    memset(t, 0, sizeof(*t));

    t->entry = fn;
    t->arg = arg;
    t->prio = (uint8_t) (attr ? attr->priority : HRT_PRIO1);
    t->timeslice_cfg = (uint16_t) (attr ? attr->timeslice : g_default_slice);
    t->stack_base = stack_words;
    t->stack_words = n_words;

    extern void hrt__task_trampoline(void);
    hrt_port_prepare_task_stack(id, hrt__task_trampoline, stack_words, n_words);

    t->state = HRT_READY;
    /* timeslice_cfg already holds the effective slice (default applied if attr==NULL) */
    t->slice_left = t->timeslice_cfg;
    rq_push(t->prio, (uint8_t) id);
    return id;
}

void hrt_start(void) {
    /* Let the port take over and run the scheduler loop */
    hrt_port_enter_scheduler();
}

static inline uint32_t hrt__ms_to_ticks(uint32_t ms, uint32_t tick_hz){
    if (ms == 0) {
        // RTOS-friendly semantics: sleep(0) == sleep(1 tick)
        return 1;
    }
    if (tick_hz == 0) {
        // Misconfiguration: no notion of time. Fallback to 1 tick so callers don't spin forever.
        return 1;
    }
    // ceil(ms * tick_hz / 1000), 64-bit to avoid overflow
    uint64_t t = (uint64_t)ms * (uint64_t)tick_hz + 999ULL;
    t /= 1000ULL;
    if (t == 0) t = 1;               // paranoia; ms>0 so ensure progress
    if (t > UINT32_MAX) t = UINT32_MAX;
    return (uint32_t)t;
}

void hrt_sleep(uint32_t ms){
    if (g_current < 0) return;

    uint32_t ticks = hrt__ms_to_ticks(ms, g_tick_hz);

    hrt_tcb_t* t = &g_tcbs[g_current];
    t->wake_tick = g_tick + ticks;    // wrap-safe checked in the tick hook
    t->state     = HRT_SLEEP;

    // Request reschedule; then voluntarily hop to scheduler for immediate handoff.
    hrt__pend_context_switch();
    hrt_port_yield_to_scheduler();
}

void hrt_yield(void) {
    if (g_current < 0) return;
    hrt_tcb_t *t = &g_tcbs[g_current];
    if (t->state == HRT_READY) {
        /* On yield, move to tail and refresh quantum (RR semantics). */
        t->slice_left = t->timeslice_cfg;
        rq_push(t->prio, (uint8_t) g_current);
    }
    hrt__pend_context_switch(); /* request reschedule */
    hrt_port_yield_to_scheduler(); /* hop to scheduler */
}

uint32_t hrt_tick_now(void) { return g_tick; }

void hrt_set_policy(hrt_policy_t p) { g_policy = p; }
void hrt_set_default_timeslice(uint16_t t) { g_default_slice = t; }

/* ------------- Internal helpers used by sched/time ------------- */
void hrt__make_ready(int id) {
    hrt_tcb_t *t = &g_tcbs[id];
    if (t->state != HRT_READY) {
        t->state = HRT_READY;
        /* Reset slice strictly to task's configured value; 0 means cooperative */
        t->slice_left = t->timeslice_cfg;
        rq_push(t->prio, (uint8_t) id);
    }
}

/* Requeue a READY task to the tail without modifying its slice/state. */
void hrt__requeue_noreset(int id) {
    hrt_tcb_t *t = &g_tcbs[id];
    if (t->state == HRT_READY) {
        rq_push(t->prio, (uint8_t) id);
    }
}

/* Selection logic, called by scheduler/ISR. Next TCB id or -1 if none. */
int hrt__pick_next_ready(void) {
    if (g_policy == HRT_SCHED_RR) {
        for (int p = 0; p < HEARTOS_MAX_PRIO; ++p) {
            int id = rq_pop((uint8_t) p);
            if (id >= 0) return id;
        }
        return -1;
    }
    /* PRIORITY or PRIORITY_RR: scan from highest priority class */
    for (int p = 0; p < HEARTOS_MAX_PRIO; ++p) {
        int id = rq_pop((uint8_t) p);
        if (id >= 0) return id;
    }
    return -1;
}

/* Expose some globals to other core files */
int hrt__get_current(void) { return g_current; }
void hrt__set_current(int id) { g_current = id; }
hrt_tcb_t *hrt__tcb(int id) { return &g_tcbs[id]; }
void hrt__inc_tick(void) { g_tick++; }
hrt_policy_t hrt__policy(void) { return g_policy; }
uint32_t hrt__cfg_core_hz(void) { return g_core_hz; }
hrt_tick_source_t hrt__cfg_tick_src(void) { return g_tick_src; }
uint32_t hrt__cfg_tick_hz(void) { return g_tick_hz; }


/* Called by the port when re-entering the scheduler from a task context. */
void hrt__on_scheduler_entry(void) {
    if (g_current < 0) return;
    hrt_tcb_t *t = &g_tcbs[g_current];
    if (t->state != HRT_READY) return;
    hrt_policy_t pol = g_policy;
    if ((pol == HRT_SCHED_RR || pol == HRT_SCHED_PRIORITY_RR) && t->timeslice_cfg > 0) {
        if (t->slice_left == 0) {
            /* Time slice expired: move running task to tail and refresh its quantum */
            t->slice_left = t->timeslice_cfg;
            rq_push(t->prio, (uint8_t) g_current);
        }
    }
}

#ifdef HEARTOS_TEST_HOOKS
/* Test-only helpers for POSIX tests: set/get the current tick safely. */
void hrt__test_set_tick(uint32_t v) {
    /* Directly set the tick counter. Ports should ensure no concurrent tick when calling this. */
    g_tick = v;
}
uint32_t hrt__test_get_tick(void) { return g_tick; }
#endif
