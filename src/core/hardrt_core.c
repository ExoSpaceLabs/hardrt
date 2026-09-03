/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>

#include "hardrt.h"
#include "hardrt_port.h"
#include "hardrt_cfg.h"
#include "hardrt_time.h"
#include "hardrt_port_int.h"

#ifndef HARDRT_STALL_ON_ERROR
#define HARDRT_STALL_ON_ERROR 0
#endif

#ifndef HARDRT_DEBUG
#define HARDRT_DEBUG 0
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
static uint8_t g_explicit_yield = 0u;
static uint32_t g_ready_prio_mask = 0u;
volatile hrt_err g_error = NONE;

#if HARDRT_DEBUG == 1
volatile int dbg_pick;
volatile int dbg_id_save;
volatile uint32_t dbg_sp_save;
volatile int dbg_id_load;
volatile uint32_t dbg_sp_load;
volatile int dbg_ct_id;
volatile uint32_t dbg_ct_sp;
volatile uint32_t dbg_tsk_q = 0;
volatile uint32_t dbg_make_ready_id;
volatile uint32_t dbg_make_ready_state;
volatile uint32_t dbg_pend_from_core;
#endif

/*
 * Ready queues are intrusive task-ID FIFOs. Each task contributes one next-ID
 * byte, while each priority owns only head/tail/count state. This keeps ready
 * storage O(HARDRT_MAX_TASKS + HARDRT_MAX_PRIO) and removes ring-buffer modulo
 * arithmetic from the scheduler hot path.
 */
#define HRT_RQ_NONE UINT8_MAX

typedef struct {
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} prio_q_t;

static prio_q_t g_rq[HARDRT_MAX_PRIO];
static uint8_t g_rq_next[HARDRT_MAX_TASKS];

/*
 * Sleeping tasks use one global intrusive delta queue. g_sleep_delta[id] is
 * relative to the preceding node, so the tick path only touches the head when
 * nothing expires. Equal deadlines are represented by zero-delta followers.
 * Insertion is bounded O(N) in task context; tick work is O(1) without expiry
 * and O(K) when K tasks actually wake.
 */
#define HRT_SLEEP_NONE UINT8_MAX
static uint8_t g_sleep_head = HRT_SLEEP_NONE;
static uint8_t g_sleep_next[HARDRT_MAX_TASKS];
static uint32_t g_sleep_delta[HARDRT_MAX_TASKS];

_hrt_tcb_t *hrt__tcb(const int id) {
#if HARDRT_DEBUG == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) return NULL;
#endif
    return &g_tcbs[id];
}

void hrt__init_idle_task(void);
void hrt_port_enter_scheduler(void);
void hrt_port_yield_to_scheduler(void);

/* ------------- Queue helpers ------------- */
#if HARDRT_DEBUG == 1
static int rq_contains(const uint8_t p, const int id) {
    if (p >= HARDRT_MAX_PRIO) return 0;
    const prio_q_t *q = &g_rq[p];
    uint8_t current = q->head;
    for (uint8_t n = 0u; n < q->count; ++n) {
        if (current == HRT_RQ_NONE || current >= HARDRT_MAX_TASKS) return 0;
        if ((int)current == id) return 1;
        current = g_rq_next[current];
    }
    return 0;
}
#endif

static uint8_t rq_first_ready_priority(const uint32_t mask) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_ctz(mask);
#else
    uint32_t value = mask;
    uint8_t p = 0u;
    if ((value & 0x0000FFFFu) == 0u) { value >>= 16u; p = (uint8_t)(p + 16u); }
    if ((value & 0x000000FFu) == 0u) { value >>= 8u; p = (uint8_t)(p + 8u); }
    if ((value & 0x0000000Fu) == 0u) { value >>= 4u; p = (uint8_t)(p + 4u); }
    if ((value & 0x00000003u) == 0u) { value >>= 2u; p = (uint8_t)(p + 2u); }
    if ((value & 0x00000001u) == 0u) p = (uint8_t)(p + 1u);
    return p;
#endif
}

static int rq_validate_push(const uint8_t p, const int id) {
    if (p >= HARDRT_MAX_PRIO) {
#if HARDRT_DEBUG == 1
        dbg_tsk_q = p;
#endif
        hrt_error(ERR_INVALID_PRIO);
        return 0;
    }
#if HARDRT_DEBUG == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        dbg_tsk_q = (uint32_t)id;
        hrt_error(ERR_INVALID_ID);
        return 0;
    }
    if (rq_contains(p, id)) {
        dbg_tsk_q = (uint32_t)id;
        hrt_error(ERR_DUP_READY);
        return 0;
    }
#endif
    if (g_rq[p].count >= HARDRT_MAX_TASKS) {
#if HARDRT_DEBUG == 1
        dbg_tsk_q = g_rq[p].count;
#endif
        hrt_error(ERR_RQ_OVERFLOW);
        return 0;
    }
    return 1;
}

static void rq_push(const uint8_t p, const int id) {
    if (!rq_validate_push(p, id)) return;
    prio_q_t *q = &g_rq[p];
    const uint8_t task_id = (uint8_t)id;

    g_rq_next[task_id] = HRT_RQ_NONE;
    if (q->count == 0u) {
        q->head = task_id;
    } else {
        g_rq_next[q->tail] = task_id;
    }
    q->tail = task_id;
    q->count++;
    g_ready_prio_mask |= (uint32_t)(1u << p);
#if HARDRT_DEBUG == 1
    dbg_tsk_q = q->count;
#endif
}

static void rq_push_front(const uint8_t p, const int id) {
    if (!rq_validate_push(p, id)) return;
    prio_q_t *q = &g_rq[p];
    const uint8_t task_id = (uint8_t)id;

    g_rq_next[task_id] = q->head;
    q->head = task_id;
    if (q->count == 0u) q->tail = task_id;
    q->count++;
    g_ready_prio_mask |= (uint32_t)(1u << p);
#if HARDRT_DEBUG == 1
    dbg_tsk_q = q->count;
#endif
}

static int rq_pop(const uint8_t p) {
#if HARDRT_DEBUG == 1
    if (p >= HARDRT_MAX_PRIO) {
        dbg_tsk_q = p;
        hrt_error(ERR_INVALID_PRIO);
        return -1;
    }
#endif
    prio_q_t *q = &g_rq[p];
    if (q->count == 0u) {
#if HARDRT_DEBUG == 1
        dbg_tsk_q = 0u;
#endif
        return -1;
    }

    const uint8_t task_id = q->head;
#if HARDRT_DEBUG == 1
    if (task_id == HRT_RQ_NONE || task_id >= HARDRT_MAX_TASKS) {
        dbg_tsk_q = (uint32_t)-2000;
        hrt_error(ERR_INVALID_ID_FROM_RQ);
        return -1;
    }
#endif

    q->head = g_rq_next[task_id];
    g_rq_next[task_id] = HRT_RQ_NONE;
    q->count--;
    if (q->count == 0u) {
        q->head = HRT_RQ_NONE;
        q->tail = HRT_RQ_NONE;
        g_ready_prio_mask &= ~(uint32_t)(1u << p);
    }
#if HARDRT_DEBUG == 1
    dbg_tsk_q = q->count;
#endif
    return (int)task_id;
}

static void sleepq_insert(const int id, const uint32_t ticks) {
    const uint8_t task_id = (uint8_t)id;
    uint8_t prev = HRT_SLEEP_NONE;
    uint8_t current = g_sleep_head;
    uint32_t remaining = ticks;

    /* Advance while the new deadline is at or after the current node. For an
     * equal deadline, walk past any zero-delta followers so equal-time wakes
     * retain insertion order. */
    while (current != HRT_SLEEP_NONE && remaining >= g_sleep_delta[current]) {
        remaining -= g_sleep_delta[current];
        prev = current;
        current = g_sleep_next[current];
        if (remaining == 0u) {
            while (current != HRT_SLEEP_NONE && g_sleep_delta[current] == 0u) {
                prev = current;
                current = g_sleep_next[current];
            }
            break;
        }
    }

    g_sleep_delta[task_id] = remaining;
    g_sleep_next[task_id] = current;
    if (current != HRT_SLEEP_NONE) g_sleep_delta[current] -= remaining;

    if (prev == HRT_SLEEP_NONE) {
        g_sleep_head = task_id;
    } else {
        g_sleep_next[prev] = task_id;
    }
}

/* Helper to fetch/store SP for a given task id. */
uint32_t *_get_sp(const int id) {
    return hrt__tcb(id)->sp;
}

void _set_sp(const int id, uint32_t *sp) {
    if (sp == NULL) hrt_error(ERR_SP_NULL);
    hrt__tcb(id)->sp = sp;
}

/* ------------- Core API ------------- */
int hrt_init(const hrt_config_t *cfg) {
    memset(g_tcbs, 0, sizeof(g_tcbs));
    memset(g_rq, 0, sizeof(g_rq));
    memset(g_rq_next, HRT_RQ_NONE, sizeof(g_rq_next));
    memset(g_sleep_next, HRT_SLEEP_NONE, sizeof(g_sleep_next));
    memset(g_sleep_delta, 0, sizeof(g_sleep_delta));
    for (int i = 0; i < HARDRT_MAX_TASKS; ++i) g_tcbs[i].state = HRT_UNUSED;
    for (int p = 0; p < HARDRT_MAX_PRIO; ++p) {
        g_rq[p].head = HRT_RQ_NONE;
        g_rq[p].tail = HRT_RQ_NONE;
    }

    g_tick = 0;
    g_current = -1;
    g_explicit_yield = 0u;
    g_ready_prio_mask = 0u;
    g_sleep_head = HRT_SLEEP_NONE;

    if (cfg) {
        g_tick_hz = cfg->tick_hz ? cfg->tick_hz : 1000;
        g_policy = cfg->policy;
        g_default_slice = cfg->default_slice ? cfg->default_slice : 5;
        g_core_hz = cfg->core_hz;
        g_tick_src = cfg->tick_src;
    } else {
        g_tick_hz = 1000;
        g_policy = HRT_SCHED_PRIORITY_RR;
        g_default_slice = 5;
        g_core_hz = 0;
        g_tick_src = HRT_TICK_SYSTICK;
    }

    hrt_port_start_systick(g_tick_hz);
    hrt__init_idle_task();
    return 0;
}

int hrt_create_task(hrt_task_fn fn, void *arg,
                    uint32_t *stack_words, size_t n_words,
                    const hrt_task_attr_t *attr) {
    if (!fn || !stack_words || n_words < 64u) {
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
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
        return -1;
    }
    memset(t, 0, sizeof(*t));

    t->entry = fn;
    t->arg = arg;
    t->prio = (uint8_t)(attr ? attr->priority : HRT_PRIO1);
    t->timeslice_cfg = (uint16_t)(attr ? attr->timeslice : g_default_slice);
    t->stack_base = stack_words;
    t->stack_words = n_words;

    hrt_port_prepare_task_stack(id, hrt__task_trampoline, stack_words, n_words);
#if HARDRT_DEBUG == 1
    dbg_ct_id = id;
    dbg_ct_sp = (uintptr_t)t->sp;
#endif
    t->state = HRT_READY;
    t->slice_left = t->timeslice_cfg;
    rq_push(t->prio, id);
    return id;
}

void hrt_start(void) {
    hrt__pend_context_switch();
    hrt_port_enter_scheduler();
}

static inline uint32_t hrt__ms_to_ticks(const uint32_t ms, const uint32_t tick_hz) {
    if (ms == 0u || tick_hz == 0u) {
        /* Current v0.4 behavior: sleep(0) sleeps for one tick. #30 changes this in v0.5. */
        return 1u;
    }
    uint64_t t = (uint64_t)ms * (uint64_t)tick_hz + 999ULL;
    t /= 1000ULL;
    if (t == 0u) t = 1u;
    if (t > UINT32_MAX) t = UINT32_MAX;
    return (uint32_t)t;
}

void hrt_sleep(const uint32_t ms) {
#if HARDRT_DEBUG == 1
    if (g_current < 0) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    _hrt_tcb_t *t = hrt__tcb(g_current);
#if HARDRT_DEBUG == 1
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif

    const uint32_t ticks = hrt__ms_to_ticks(ms, g_tick_hz);

    /* Publish SLEEP state, delta-queue membership, and the reschedule request
     * atomically with respect to the tick source. This prevents a one-tick
     * sleeper from being woken/requeued while it is still the running task. */
    hrt_port_crit_enter();
    t->wake_tick = g_tick + ticks;
    t->state = HRT_SLEEP;
    sleepq_insert(g_current, ticks);
#if HARDRT_DEBUG == 1
    dbg_pend_from_core++;
#endif
    hrt__pend_context_switch();
    hrt_port_crit_exit();
    hrt_port_yield_to_scheduler();
}

void hrt_yield(void) {
#if HARDRT_DEBUG == 1
    if (g_current < 0) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    _hrt_tcb_t *t = hrt__tcb(g_current);
#if HARDRT_DEBUG == 1
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif
    if (t->state == HRT_READY) g_explicit_yield = 1u;
#if HARDRT_DEBUG == 1
    dbg_pend_from_core++;
#endif
    hrt__pend_context_switch();
    hrt_port_yield_to_scheduler();
}

void hrt_task_delete(void) {
    const int cur = hrt__get_current();
    if (cur < 0 || cur == HRT_IDLE_ID) return;

    hrt_port_crit_enter();
    _hrt_tcb_t *t = hrt__tcb(cur);
    if (t) t->state = HRT_UNUSED;
    hrt_port_crit_exit();

    hrt__pend_context_switch();
    hrt_port_yield_to_scheduler();
}

uint32_t hrt_tick_now(void) { return g_tick; }

uint32_t hrt_now_ms(void) {
    if (g_tick_hz == 0u) return 0u;
    return (uint32_t)(((uint64_t)g_tick * 1000ULL) / (uint64_t)g_tick_hz);
}

void hrt_set_policy(const hrt_policy_t p) { g_policy = p; }
void hrt_set_default_timeslice(const uint16_t t) { g_default_slice = t; }

/* ------------- Internal helpers used by scheduler/time/IPC ------------- */
void hrt__make_ready(const int id) {
#if HARDRT_DEBUG == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    _hrt_tcb_t *t = hrt__tcb(id);
#if HARDRT_DEBUG == 1
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
    if (t->state == HRT_READY) {
        hrt_error(ERR_DUP_READY);
        return;
    }
    dbg_make_ready_id = (uint32_t)id;
    dbg_make_ready_state = t->state;
#endif

    t->state = HRT_READY;
    t->slice_left = t->timeslice_cfg;
    rq_push(t->prio, id);
}

void hrt__requeue_noreset(const int id) {
#if HARDRT_DEBUG == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    const _hrt_tcb_t *t = hrt__tcb(id);
#if HARDRT_DEBUG == 1
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif
    if (t->state == HRT_READY) rq_push(t->prio, id);
}

void hrt__requeue_front_noreset(const int id) {
#if HARDRT_DEBUG == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    const _hrt_tcb_t *t = hrt__tcb(id);
#if HARDRT_DEBUG == 1
    if (t == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
#endif
    if (t->state == HRT_READY) rq_push_front(t->prio, id);
}

/* Prepare the outgoing running task exactly once before choosing its successor.
 * State + explicit-yield + slice exhaustion encode the current reschedule cause:
 * - blocked/sleeping/deleted: do not requeue;
 * - explicit yield: rotate to tail and refresh quantum;
 * - RR quantum expiry: rotate to tail and refresh quantum;
 * - otherwise: retain precedence at the front with remaining quantum untouched.
 */
void hrt__prepare_current_for_reschedule(void) {
    const int cur = g_current;
    if (cur < 0 || cur >= HARDRT_MAX_TASKS || cur == HRT_IDLE_ID) {
        g_explicit_yield = 0u;
        return;
    }

    _hrt_tcb_t *t = hrt__tcb(cur);
    if (t == NULL || t->state != HRT_READY) {
        g_explicit_yield = 0u;
        return;
    }

    const int rr_policy = (g_policy == HRT_SCHED_RR || g_policy == HRT_SCHED_PRIORITY_RR);
    if (g_explicit_yield != 0u) {
        t->slice_left = t->timeslice_cfg;
        rq_push(t->prio, cur);
    } else if (rr_policy && t->timeslice_cfg > 0u && t->slice_left == 0u) {
        t->slice_left = t->timeslice_cfg;
        rq_push(t->prio, cur);
    } else {
        rq_push_front(t->prio, cur);
    }

    g_explicit_yield = 0u;
}

int hrt__should_preempt_after_wake(const int woken_id) {
    if (woken_id < 0 || woken_id >= HARDRT_MAX_TASKS) return 0;

    const int cur = g_current;
    if (cur < 0 || cur >= HARDRT_MAX_TASKS || cur == HRT_IDLE_ID) return 1;

    const _hrt_tcb_t *woken = hrt__tcb(woken_id);
    const _hrt_tcb_t *current = hrt__tcb(cur);
    if (woken == NULL || current == NULL) return 0;
    if (current->state != HRT_READY) return 1;

    if (g_policy == HRT_SCHED_PRIORITY || g_policy == HRT_SCHED_PRIORITY_RR) {
        return woken->prio < current->prio;
    }

    /* Global RR wake semantics are finalized in #28. Preserve the legacy
       scheduling opportunity for HRT_SCHED_RR until that policy is rebuilt. */
    if (g_policy == HRT_SCHED_RR) return 1;
    return 0;
}

int hrt__sleep_tick(void) {
    uint8_t trigger_pendsv = 0u;

    if (g_sleep_head == HRT_SLEEP_NONE) return 0;

    if (g_sleep_delta[g_sleep_head] > 0u) g_sleep_delta[g_sleep_head]--;

    while (g_sleep_head != HRT_SLEEP_NONE && g_sleep_delta[g_sleep_head] == 0u) {
        const int id = (int)g_sleep_head;
        _hrt_tcb_t *t = hrt__tcb(id);
        g_sleep_head = g_sleep_next[id];
        g_sleep_next[id] = HRT_SLEEP_NONE;
        g_sleep_delta[id] = 0u;

#if HARDRT_DEBUG == 1
        if (t == NULL) {
            hrt_error(ERR_TCB_NULL);
            continue;
        }
        if (t->state != HRT_SLEEP) {
            hrt_error(ERR_INVALID_TASK);
            continue;
        }
#endif

        /* Decide before changing the task state. This matters when the sleeper
         * is still recorded as current while the scheduler is idle. */
        const int should_switch = hrt__should_preempt_after_wake(id);
        hrt__make_ready(id);
        if (should_switch) trigger_pendsv = 1u;
    }

    return (int)trigger_pendsv;
}

int hrt__pick_next_ready(void) {
    int id = HRT_IDLE_ID;
    if (g_ready_prio_mask != 0u) {
        const uint8_t p = rq_first_ready_priority(g_ready_prio_mask);
        const int candidate = rq_pop(p);
        if (candidate >= 0) id = candidate;
    }
#if HARDRT_DEBUG == 1
    dbg_pick = id;
#endif
    return id;
}

int hrt__get_current(void) { return g_current; }

void hrt__set_current(const int id) {
#if HARDRT_DEBUG == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return;
    }
#endif
    g_current = id;
}

void hrt__inc_tick(void) { g_tick++; }
hrt_policy_t hrt__policy(void) { return g_policy; }
uint32_t hrt__cfg_core_hz(void) { return g_core_hz; }
hrt_tick_source_t hrt__cfg_tick_src(void) { return g_tick_src; }
uint32_t hrt__cfg_tick_hz(void) { return g_tick_hz; }

void hrt__save_current_sp(const uintptr_t sp) {
    const int cur = hrt__get_current();
    _set_sp(cur, (uint32_t *)sp);
#if HARDRT_DEBUG == 1
    dbg_id_save = cur;
    dbg_sp_save = (uint32_t)sp;
#endif
}

uintptr_t hrt__load_next_sp_and_set_current(const int next_id) {
    const uintptr_t sp = (uintptr_t)_get_sp(next_id);
    hrt__set_current(next_id);
#if HARDRT_DEBUG == 1
    dbg_id_load = next_id;
    dbg_sp_load = (uint32_t)sp;
#endif
    return sp;
}

void hrt__on_scheduler_entry(void) {
    hrt__prepare_current_for_reschedule();
}

__attribute__((noinline, used))
void hrt_error(const hrt_err code) {
    g_error = code;
#if HARDRT_STALL_ON_ERROR == 1
    for (;;) {}
#endif
}

#ifdef HARDRT_TEST_HOOKS
void hrt__test_set_tick(uint32_t v) { g_tick = v; }
uint32_t hrt__test_get_tick(void) { return g_tick; }

uint16_t hrt__test_task_slice_left(int id) {
    if (id < 0 || id >= HARDRT_MAX_TASKS) return 0u;
    return hrt__tcb(id)->slice_left;
}

int hrt__test_ready_occurrences(int id) {
    if (id < 0 || id >= HARDRT_MAX_TASKS) return 0;
    int count = 0;
    for (int p = 0; p < HARDRT_MAX_PRIO; ++p) {
        const prio_q_t *q = &g_rq[p];
        uint8_t current = q->head;
        for (uint8_t n = 0u; n < q->count; ++n) {
            if (current == HRT_RQ_NONE || current >= HARDRT_MAX_TASKS) break;
            if ((int)current == id) count++;
            current = g_rq_next[current];
        }
    }
    return count;
}

uint32_t hrt__test_ready_prio_mask(void) { return g_ready_prio_mask; }
#endif

uintptr_t hrt__schedule(const uintptr_t old_sp) {
    if (old_sp) {
#if HARDRT_DEBUG == 1
        if ((unsigned)g_current >= (unsigned)HARDRT_MAX_TASKS) {
            hrt_error(ERR_INVALID_ID);
        }
#endif
        _hrt_tcb_t *const cur = hrt__tcb(g_current);
#if HARDRT_DEBUG == 1
        if (!cur) hrt_error(ERR_TCB_NULL);
#endif
        _set_sp(g_current, (uint32_t *)old_sp);
        hrt__prepare_current_for_reschedule();
    }

    const int next_id = hrt__pick_next_ready();
#if HARDRT_DEBUG == 1
    dbg_id_save = g_current;
    dbg_sp_save = (uint32_t)old_sp;
    dbg_pick = next_id;
    if ((unsigned)next_id >= (unsigned)HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_NEXT_ID);
    }
#endif

    hrt__set_current(next_id);
    const uintptr_t sp_new = (uintptr_t)_get_sp(next_id);
#if HARDRT_DEBUG == 1
    dbg_id_load = next_id;
    dbg_sp_load = (uint32_t)sp_new;
#endif
    return sp_new;
}