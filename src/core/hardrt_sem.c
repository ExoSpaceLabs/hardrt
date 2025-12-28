/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_sem.h"
#include "hardrt_time.h"

/* Local debug prints for this file only. Not overridable from outside. */
#undef HRT_SEM_DEBUG
#define HRT_SEM_DEBUG 0

#ifndef HARDRT_DEBUG
    #define HARDRT_BDG_VARIABLES 0
    #define HARDRT_VALIDATION 0
#else
    #define HARDRT_BDG_VARIABLES 1
    #define HARDRT_VALIDATION 1
#endif


#if HRT_SEM_DEBUG || defined(HARDRT_TEST_HOOKS)
#include <stdio.h>
#endif

/* Core-private hooks */
int hrt__get_current(void);

void hrt__set_current(int id);

void hrt__make_ready(int id);

extern _hrt_tcb_t *hrt__tcb(int id);

/* Port-provided critical section (non-nestable minimal CS) */
void hrt_port_crit_enter(void);

void hrt_port_crit_exit(void);

/* Internal: enqueue/dequeue waiter (FIFO) */
static void _waitq_push(hrt_sem_t *s, const uint8_t id) {
#if HARDRT_VALIDATION == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
    }
#endif
    if (s->count_wait >= HARDRT_MAX_TASKS) return;
    s->q[s->tail] = id;
    s->tail = (uint8_t) ((s->tail + 1) % HARDRT_MAX_TASKS);
    s->count_wait++;
}

static int _waitq_pop(hrt_sem_t *s) {
    if (!s->count_wait) return -1;
    const int id = s->q[s->head];

#if HARDRT_VALIDATION == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
    };
#endif

    s->head = (uint8_t) ((s->head + 1) % HARDRT_MAX_TASKS);
    s->count_wait--;
    return id;
}

int hrt_sem_try_take(hrt_sem_t *s) {
    int ok = -1;
    hrt_port_crit_enter();
    if (s->count) {
        s->count = 0;
        ok = 0;
    }
    hrt_port_crit_exit();
    return ok;
}

int hrt_sem_take(hrt_sem_t *s) {
    /* Fast-path: try without blocking */
    if (hrt_sem_try_take(s) == 0) return 0;

    /* Block current task */
    int me = hrt__get_current();

#if HARDRT_VALIDATION == 1
    if (me < 0 || me >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return -1;
    }
#endif

    hrt_port_crit_enter();

    /* Re-check after taking CS */
    if (s->count) {
        s->count = 0;
        hrt_port_crit_exit();
        return 0;
    }

    /* Put current into the semaphore wait queue and mark blocked */
    _waitq_push(s, (uint8_t) me);
#if DEBUG
    printf("[sem] take: task %d queued, waiters=%u\n", me, (unsigned) s->count_wait);
#endif
    _hrt_tcb_t *t = hrt__tcb(me);

#if HARDRT_VALIDATION == 1
    if (!t){hrt_error(ERR_TCB_NULL);}
#endif

    t->state = HRT_BLOCKED;

    /* Request rescheduling and yield to scheduler from the task context */
    extern void hrt_port_yield_to_scheduler(void);

    hrt_port_crit_exit();

    hrt__pend_context_switch();
    hrt_port_yield_to_scheduler();

    /* When we resume, we must have been given the semaphore. */
    return 0;
}

static int _give_common(hrt_sem_t *s, int is_isr, int *need_switch) {
    int woken = 0;

    hrt_port_crit_enter();

    int waiter = _waitq_pop(s);
    if (waiter >= 0) {
        /* Wake exactly one waiter */
        _hrt_tcb_t *tw = hrt__tcb(waiter);

#if HARDRT_VALIDATION == 1
        if (!tw) {hrt_error(ERR_TCB_NULL);}
#endif
        /* Do not pre-set the state here; hrt__make_ready() will set state to READY
         * and push the task into the ready queue. */
        hrt__make_ready(waiter);
        woken = 1;
#if DEBUG
        printf("[sem] give: woke waiter %d\n", waiter);
#endif
    } else {
        /* No waiter: set binary sem to available */
        s->count = 1;
#ifdef HARDRT_TEST_HOOKS
        printf("[sem] give: no waiters, set count=1\n");
#endif
    }

    hrt_port_crit_exit();

    if (is_isr) {
        if (need_switch) *need_switch = woken;
        if (woken) {

            hrt__pend_context_switch();
        }
    } else {
        if (woken) {
            /* Requeue the current task appropriately and yield, similar to hrt_yield().
             * This ensures the giver is placed back on its ready queue while
             * allowing the (potentially higher-priority) woken waiter to run. */
            hrt_yield();
        }
    }

    return 0;
}

int hrt_sem_give(hrt_sem_t *s) {
    return _give_common(s, 0, 0);
}

int hrt_sem_give_from_isr(hrt_sem_t *s, int *need_switch) {
    return _give_common(s, 1, need_switch);
}
