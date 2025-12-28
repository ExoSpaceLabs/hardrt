/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>

#include "hardrt.h"
#include "hardrt_cfg.h"
#include "hardrt_time.h"
#include "hardrt_port_int.h"
#include <string.h>

#ifndef HARDRT_STALL_ON_ERROR
    #define HARDRT_STALL_ON_ERROR 0
#endif

#ifndef HARDRT_DEBUG
    #define HARDRT_BDG_VARIABLES 0
    #define HARDRT_VALIDATION 0
#else
    #define HARDRT_BDG_VARIABLES 1
    #define HARDRT_VALIDATION 1
#endif


/* Globals */
static _hrt_tcb_t g_tcbs[HARDRT_MAX_TASKS];
static int g_current = -1;
static uint32_t g_tick = 0;
static uint32_t g_tick_hz = 1000;
static hrt_policy_t g_policy = HRT_SCHED_PRIORITY_RR;
static uint16_t g_default_slice = 5;
static uint32_t g_core_hz = 0;
static hrt_tick_source_t g_tick_src = HRT_TICK_SYSTICK;
volatile hrt_err g_error = NONE;

//TODO REMOVE DEBUG VARIABLES.
#if HARDRT_BDG_VARIABLES == 1
volatile int dbg_pick;
volatile int dbg_id_save;
volatile uint32_t dbg_sp_save;
volatile int dbg_id_load;
volatile uint32_t dbg_sp_load;
volatile int dbg_ct_id;
volatile uint32_t dbg_ct_sp;
volatile uint32_t dbg_tsk_q= 0;

volatile uint32_t dbg_make_ready_id;
volatile uint32_t dbg_make_ready_state;
volatile uint32_t dbg_pend_from_core;
#endif
/* Ready queues per priority (store task indices) */
typedef struct {
    uint8_t q[HARDRT_MAX_TASKS];
    uint8_t head, tail, count;
} prio_q_t;

static prio_q_t g_rq[HARDRT_MAX_PRIO];
_hrt_tcb_t *hrt__tcb(const int id) {

#if HARDRT_VALIDATION == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) return NULL;
#endif
    return &g_tcbs[id];
}



void hrt__init_idle_task(void);

/* Provided by the port */
void hrt_port_enter_scheduler(void);

void hrt_port_yield_to_scheduler(void);

/* ------------- Queue helpers ------------- */
static void rq_push(const uint8_t p, const int id) {
    /* Validate priority */
#if HARDRT_VALIDATION == 1
    if (p >= HARDRT_MAX_PRIO) {
#if HARDRT_BDG_VARIABLES == 1
        dbg_tsk_q = 5000;
        (void)dbg_tsk_q;
#endif
        hrt_error(ERR_INVALID_PRIO);
        return;
    }

    /* Validate task id BEFORE storing it into the queue */
    if (id < 0 || id >= HARDRT_MAX_TASKS) {

#if HARDRT_BDG_VARIABLES == 1
        dbg_tsk_q = 1000;
        (void)dbg_tsk_q;
#endif
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    prio_q_t *q = &g_rq[p];

    /* Hard fail on overflow in debug; in release you could drop or overwrite */

#if HARDRT_VALIDATION == 1
    if (q->count >= HARDRT_MAX_TASKS) {
#if HARDRT_BDG_VARIABLES == 1
        dbg_tsk_q = q->count;
        (void)dbg_tsk_q;
#endif
        hrt_error(ERR_RQ_OVERFLOW);
        return;
    }
#endif
    q->q[q->tail] = (uint8_t)id;
    q->tail = (uint8_t)((q->tail + 1u) % HARDRT_MAX_TASKS);
    q->count++;

#if HARDRT_BDG_VARIABLES == 1
    dbg_tsk_q = q->count;
    (void)dbg_tsk_q;
#endif
}

static int rq_pop(const uint8_t p) {

#if HARDRT_VALIDATION == 1
    if (p >= HARDRT_MAX_PRIO) {

#if HARDRT_BDG_VARIABLES == 1
        dbg_tsk_q = p;
        (void)dbg_tsk_q;
#endif
        hrt_error(ERR_INVALID_PRIO);
        return -1;
    }
#endif
    prio_q_t *q = &g_rq[p];
    if (q->count == 0) {

#if HARDRT_BDG_VARIABLES == 1
        dbg_tsk_q = 0;
        (void)dbg_tsk_q;
#endif
        return -1;
    }
#if HARDRT_VALIDATION == 1
    /* Hard fail on overflow in debug; in release you could drop or overwrite */
    if (q->count < 0) {

#if HARDRT_BDG_VARIABLES == 1
        dbg_tsk_q = q->count;
        (void)dbg_tsk_q;
#endif
        hrt_error(ERR_RQ_UNDERFLOW);
        return -1000;
    }
#endif
    const int id = q->q[q->head];

#if HARDRT_VALIDATION == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
#if HARDRT_BDG_VARIABLES == 1
        dbg_tsk_q = -2000;
        (void)dbg_tsk_q;
#endif
        hrt_error(ERR_INVALID_ID_FROM_RQ);
        return -1;
    }
#endif
    q->head = (uint8_t)((q->head + 1u) % HARDRT_MAX_TASKS);
    q->count--;

#if HARDRT_BDG_VARIABLES == 1
    dbg_tsk_q = q->count;
    (void)dbg_tsk_q;
#endif

    return id;
}

/* Helper to fetch/store SP for a given task id */
uint32_t *_get_sp(const int id) {
#if HARDRT_VALIDATION == 1
    // if (id < 0 || id >= HARDRT_MAX_TASKS) { hrt_error(ERR_INVALID_ID); }
    // if (!hrt__tcb(id)) {
    //     hrt_error(ERR_TCB_NULL);
    // }
    // uint32_t *sp = hrt__tcb(id)->sp;
    // hrt_port_sp_valid((uintptr_t) sp);
#endif
    return hrt__tcb(id)->sp;
}
void _set_sp(const int id, uint32_t *sp) {
    if (sp == NULL) { hrt_error(ERR_SP_NULL); }
#if HARDRT_VALIDATION == 1
    // if (id < 0 || id >= HARDRT_MAX_TASKS) { hrt_error(ERR_INVALID_ID); }
    // if (!hrt__tcb(id)) {
    //     hrt_error(ERR_TCB_NULL);
    // }
    // hrt_port_sp_valid((uintptr_t) sp);
#endif
    hrt__tcb(id)->sp = sp;
}

/* ------------- Core API ------------- */
int hrt_init(const hrt_config_t *cfg) {
    memset(g_tcbs, 0, sizeof(g_tcbs));
    memset(g_rq, 0, sizeof(g_rq));
    for (int i = 0; i < HARDRT_MAX_TASKS; ++i) g_tcbs[i].state = HRT_UNUSED;

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
    hrt__init_idle_task();
    return 0;
}

int hrt_create_task(hrt_task_fn fn, void *arg,
                    uint32_t *stack_words, size_t n_words,
                    const hrt_task_attr_t *attr) {
    if (!fn || !stack_words || n_words < 64) {
        hrt_error(ERR_INVALID_TASK);
        return -1;
    }

    int id = -1;
    for (int i = 0; i < HARDRT_MAX_TASKS; ++i) {
        if (g_tcbs[i].state == HRT_UNUSED) {
            id = i;
            break;
        }
    }

    if (id < 0) {
        hrt_error(ERR_INVALID_ID);
        return -1;
    }
    _hrt_tcb_t *t = hrt__tcb(id);

    if (t ==NULL) {
        hrt_error(ERR_TCB_NULL);
        return -1;
    }
    memset(t, 0, sizeof(*t));

    t->entry = fn;
    t->arg = arg;
    t->prio = (uint8_t) (attr ? attr->priority : HRT_PRIO1);
    t->timeslice_cfg = (uint16_t) (attr ? attr->timeslice : g_default_slice);
    t->stack_base = stack_words;
    t->stack_words = n_words;

    hrt_port_prepare_task_stack(id, hrt__task_trampoline, stack_words, n_words);
#if HARDRT_BDG_VARIABLES == 1
    dbg_ct_id = id;
    dbg_ct_sp = (uintptr_t)(t->sp);
    (void)dbg_ct_id; (void)dbg_ct_sp;
#endif
    t->state = HRT_READY;
    /* timeslice_cfg already holds the effective slice (default applied if attr==NULL) */
    t->slice_left = t->timeslice_cfg;
    rq_push(t->prio, (uint8_t) id);
    return id;
}

void hrt_start(void) {
    /* Let the port take over and run the scheduler loop */
    hrt__pend_context_switch();
    hrt_port_enter_scheduler();

}

static inline uint32_t hrt__ms_to_ticks(const uint32_t ms, const uint32_t tick_hz){
    if (ms == 0 || tick_hz == 0) {
        // RTOS-friendly semantics: sleep(0) == sleep(1 tick)
        // Misconfiguration: no notion of time. Fallback to 1 tick so callers don't spin forever.
        return 1;
    }
    // ceil(ms * tick_hz / 1000), 64-bit to avoid overflow
    uint64_t t = (uint64_t)ms * (uint64_t)tick_hz + 999ULL;
    t /= 1000ULL;
    if (t == 0) t = 1;               // paranoia; ms>0 so ensure progress
    if (t > UINT32_MAX) t = UINT32_MAX;
    return (uintptr_t)t;
}

void hrt_sleep(const uint32_t ms){

#if HARDRT_VALIDATION == 1
    if (g_current < 0) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif

    _hrt_tcb_t* t = hrt__tcb(g_current);

#if HARDRT_VALIDATION == 1
    if (t ==NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif

    const uint32_t ticks = hrt__ms_to_ticks(ms, g_tick_hz);
    t->wake_tick = g_tick + ticks;    // wrap-safe checked in the tick hook
    t->state     = HRT_SLEEP;

    // Request rescheduling; then voluntarily hop to scheduler for immediate handoff.

#if HARDRT_BDG_VARIABLES == 1
    dbg_pend_from_core++;
#endif
    hrt__pend_context_switch();
    hrt_port_yield_to_scheduler();
}

void hrt_yield(void) {
#if HARDRT_VALIDATION == 1
    if (g_current < 0) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    _hrt_tcb_t *t = hrt__tcb(g_current);

#if HARDRT_VALIDATION == 1
    if (t ==NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif
    if (t->state == HRT_READY) {
        /* On yield, move to tail and refresh quantum (RR semantics). */
        t->slice_left = t->timeslice_cfg;
        rq_push(t->prio, (uint8_t) g_current);
    }
#if HARDRT_BDG_VARIABLES == 1
    dbg_pend_from_core++;
#endif
    hrt__pend_context_switch(); /* request reschedule */
    hrt_port_yield_to_scheduler(); /* hop to scheduler */
}

uint32_t hrt_tick_now(void) { return g_tick; }

void hrt_set_policy(const hrt_policy_t p) { g_policy = p; }
void hrt_set_default_timeslice(const uint16_t t) { g_default_slice = t; }

/* ------------- Internal helpers used by sched/time ------------- */
void hrt__make_ready(const int id) {

#if HARDRT_VALIDATION == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    _hrt_tcb_t *t = hrt__tcb(id);

#if HARDRT_VALIDATION == 1
    if (t ==NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
    if (t->state == HRT_READY) {
        hrt_error(ERR_DUP_READY);
        return;
    }

#endif
#if HARDRT_BDG_VARIABLES == 1
    dbg_make_ready_id = id;
    dbg_make_ready_state = t->state;
    (void)dbg_make_ready_id;
    (void)dbg_make_ready_state;
#endif

    t->state = HRT_READY;
    /* Reset slice strictly to the task's configured value; 0 means cooperative */
    t->slice_left = t->timeslice_cfg;
    rq_push(t->prio, (uint8_t) id);

}

/* Requeue a READY task to the tail without modifying its slice/state. */
void hrt__requeue_noreset(const int id) {

#if HARDRT_VALIDATION == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    const _hrt_tcb_t *t = hrt__tcb(id);

#if HARDRT_VALIDATION == 1
    if (t ==NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif
    if (t->state == HRT_READY) {
        rq_push(t->prio, (uint8_t) id);
    }
}

/* Selection logic, called by scheduler/ISR. Next TCB id or HRT_IDLE_ID if none. */
int hrt__pick_next_ready(void)
{
    int id = HRT_IDLE_ID;

    for (int p = 0; p < HARDRT_MAX_PRIO; ++p) {
        const int candidate = rq_pop((uint8_t)p);

        if (candidate >= 0) {
            id = candidate;
            break;             // only break when we actually found a runnable task
        }
    }

#if HARDRT_BDG_VARIABLES == 1
    dbg_pick = id;
    (void)dbg_pick;
#endif
    return id;                 // HRT_IDLE_ID only if *all* priorities were empty
}

/* Expose some globals to other core files */
int hrt__get_current(void) { return g_current; }
void hrt__set_current(const int id) {

#if HARDRT_VALIDATION == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    g_current = id;
    (void)g_current;
}

void hrt__inc_tick(void) { g_tick++; }
hrt_policy_t hrt__policy(void) { return g_policy; }
uint32_t hrt__cfg_core_hz(void) { return g_core_hz; }
hrt_tick_source_t hrt__cfg_tick_src(void) { return g_tick_src; }
uint32_t hrt__cfg_tick_hz(void) { return g_tick_hz; }

void hrt__save_current_sp(const uint32_t sp)
{
    const int cur = hrt__get_current();

    _set_sp(cur, (uint32_t*)sp);

#if HARDRT_BDG_VARIABLES == 1
    dbg_id_save = cur;
    dbg_sp_save = sp;
    (void)dbg_sp_save; (void)dbg_id_save;
#endif
}

uint32_t hrt__load_next_sp_and_set_current(const int next_id){


    //__asm volatile ("mrs %0, psp" : "=r"(cur_sp));

    const uint32_t sp = (uintptr_t)(_get_sp(next_id));
    hrt__set_current(next_id);

#if HARDRT_BDG_VARIABLES == 1
    dbg_id_load = next_id;
    dbg_sp_load = sp;
    (void)dbg_id_load;
    (void)dbg_sp_load;
#endif

    return sp;
}


/* Called by the port when re-entering the scheduler from a task context. */
void hrt__on_scheduler_entry(void) {
    _hrt_tcb_t *t = hrt__tcb(g_current);
#if HARDRT_VALIDATION == 1
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif
    if (t->state != HRT_READY) return;
    const hrt_policy_t pol = g_policy;
    if ((pol == HRT_SCHED_RR || pol == HRT_SCHED_PRIORITY_RR) && t->timeslice_cfg > 0) {
        if (t->slice_left == 0) {
            /* Time slice expired: move a running task to tail and refresh its quantum */
            t->slice_left = t->timeslice_cfg;
            rq_push(t->prio, (uint8_t) g_current);
        }
    }
}

__attribute__((noinline, used))
void hrt_error(const hrt_err code) {
    g_error = code;
    (void)g_error;
    //__asm volatile("bkpt #0");
#if HARDRT_STALL_ON_ERROR == 1
    for (;;) {}
#endif
}

#ifdef HARDRT_TEST_HOOKS
/* Test-only helpers for POSIX tests: set/get the current tick safely. */
void hrt__test_set_tick(uint32_t v) {
    /* Directly set the tick counter. Ports should ensure no concurrent tick when calling this. */
    g_tick = v;
}
uint32_t hrt__test_get_tick(void) { return g_tick; }
#endif


uint32_t hrt__schedule(const uint32_t old_sp)
{

    /* Save current only if this is not the first switch */
    if (old_sp) {
#if HARDRT_VALIDATION == 1
        if ((unsigned)g_current >= (unsigned)HARDRT_MAX_TASKS) {
            hrt_error(ERR_INVALID_ID);
        }
#endif
        _hrt_tcb_t * const cur = hrt__tcb(g_current);

#if HARDRT_VALIDATION == 1
        if (!cur) { hrt_error(ERR_TCB_NULL); }
#endif

        _set_sp(g_current, (uint32_t*)old_sp);

        if (cur->state == HRT_READY) {
            rq_push(cur->prio, g_current);
        }
    }

    const int next_id = hrt__pick_next_ready();

#if HARDRT_BDG_VARIABLES == 1
    dbg_id_save = g_current;
    dbg_sp_save = old_sp;
    dbg_pick    = next_id;
#endif

#if HARDRT_VALIDATION == 1
    if ((unsigned)next_id >= (unsigned)HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_NEXT_ID);
    }
#endif

    hrt__set_current(next_id);

    const uint32_t sp_new = (uint32_t)(uintptr_t)_get_sp(next_id);

#if HARDRT_BDG_VARIABLES == 1
    dbg_id_load = next_id;
    dbg_sp_load = sp_new;
#endif

    return sp_new;
}



