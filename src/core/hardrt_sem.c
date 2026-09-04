/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt.h"
#include "hardrt_sem.h"
#include "hardrt_time.h"
#include "hardrt_port_int.h"
#include "hardrt_timing.h"

#undef HRT_SEM_DEBUG
#define HRT_SEM_DEBUG 0

#ifndef HARDRT_DEBUG
#define HARDRT_DEBUG 0
#endif

#if HRT_SEM_DEBUG || defined(HARDRT_TEST_HOOKS)
#include <stdio.h>
#endif

static void _waitq_push(hrt_sem_t *s, const uint8_t id) {
#if HARDRT_DEBUG == 1
    if (id >= HARDRT_MAX_TASKS) hrt_error(ERR_INVALID_ID);
#endif
    if (s->count_wait >= HARDRT_MAX_TASKS) return;
    s->q[s->tail] = id;
    s->tail = (uint8_t)((s->tail + 1u) % HARDRT_MAX_TASKS);
    s->count_wait++;
}

static int _waitq_pop(hrt_sem_t *s) {
    if (!s->count_wait) return -1;
    const int id = s->q[s->head];
#if HARDRT_DEBUG == 1
    if (id < 0 || id >= HARDRT_MAX_TASKS) hrt_error(ERR_INVALID_ID);
#endif
    s->head = (uint8_t)((s->head + 1u) % HARDRT_MAX_TASKS);
    s->count_wait--;
    return id;
}

void hrt_sem_init_counting(hrt_sem_t *s, unsigned init, uint8_t max_count) {
    if (max_count == 0u) max_count = 1u;
    s->max_count = max_count;
    if (init > max_count) init = max_count;
    s->count = (uint8_t)init;
    s->head = s->tail = s->count_wait = 0;
}

int hrt_sem_try_take(hrt_sem_t *s) {
    int ok = -1;
    hrt_port_crit_enter();
    if (s->count) {
        s->count--;
        ok = 0;
    }
    hrt_port_crit_exit();
    return ok;
}

int hrt_sem_take(hrt_sem_t *s) {
    if (hrt_sem_try_take(s) == 0) return 0;

    const int me = hrt__get_current();
#if HARDRT_DEBUG == 1
    if (me < 0 || me >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_INVALID_ID);
        return -1;
    }
#endif

    hrt_port_crit_enter();
    if (s->count) {
        s->count--;
        hrt_port_crit_exit();
        return 0;
    }

    _waitq_push(s, (uint8_t)me);
#if HRT_SEM_DEBUG
    printf("[sem] take: task %d queued, waiters=%u\n", me, (unsigned)s->count_wait);
#endif
    _hrt_tcb_t *t = hrt__tcb(me);
#if HARDRT_DEBUG == 1
    if (!t) hrt_error(ERR_TCB_NULL);
#endif
    t->state = HRT_BLOCKED;

    hrt_port_crit_exit();
    hrt__pend_context_switch();
    hrt_port_yield_to_scheduler();
    return 0;
}

static int _give_common(hrt_sem_t *s, int is_isr, int *need_switch) {
    int should_switch = 0;

    if (is_isr) HRT_TIMING_ISR_IPC_ENTRY();
    hrt_port_crit_enter();

    const int waiter = _waitq_pop(s);
    if (waiter >= 0) {
#if HARDRT_DEBUG == 1
        if (!hrt__tcb(waiter)) hrt_error(ERR_TCB_NULL);
#endif
        hrt__make_ready(waiter);
        if (is_isr) HRT_TIMING_ISR_WAITER_READY(waiter);
        should_switch = hrt__should_preempt_after_wake(waiter);
#ifdef HARDRT_TEST_HOOKS
        printf("[sem] give: woke waiter %d\n", waiter);
#endif
    } else {
        if (s->count < s->max_count) s->count++;
#ifdef HARDRT_TEST_HOOKS
        printf("[sem] give: no waiters, count=%u (max=%u)\n", (unsigned)s->count, (unsigned)s->max_count);
#endif
    }

    hrt_port_crit_exit();
    if (is_isr) HRT_TIMING_ISR_IPC_EXIT();

    if (is_isr) {
        if (need_switch) *need_switch = should_switch;
        if (should_switch) hrt__pend_context_switch();
    } else if (should_switch) {
        /* Priority preemption is not an explicit yield: preserve the outgoing
           task's queue precedence and remaining RR quantum. */
        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }
    return 0;
}

int hrt_sem_give(hrt_sem_t *s) {
    return _give_common(s, 0, 0);
}

int hrt_sem_give_from_isr(hrt_sem_t *s, int *need_switch) {
    return _give_common(s, 1, need_switch);
}
