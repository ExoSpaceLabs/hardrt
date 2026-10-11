/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_mutex.h"
#include "hardrt_port_int.h"
#include "hardrt_deadline.h"

/* All _locked helpers require the kernel critical section. */
static int waitq_push(hrt_mutex_t *m, uint8_t id) {
    if (m->count_wait >= HARDRT_APP_MAX_TASKS) return -1;
    m->q[m->tail] = id;
    m->tail = (uint8_t)((m->tail + 1u) % HARDRT_APP_MAX_TASKS);
    m->count_wait++;
    return 0;
}

static int waitq_pop(hrt_mutex_t *m) {
    if (m->count_wait == 0u) return -1;
    const int id = m->q[m->head];
    m->head = (uint8_t)((m->head + 1u) % HARDRT_APP_MAX_TASKS);
    m->count_wait--;
    return id;
}

static int waitq_remove(hrt_mutex_t *m, uint8_t id) {
    const uint8_t count = m->count_wait;
    uint8_t offset = 0u;
    while (offset < count) {
        const uint8_t pos = (uint8_t)((m->head + offset) % HARDRT_APP_MAX_TASKS);
        if (m->q[pos] == id) break;
        offset++;
    }
    if (offset == count) return -1;
    for (uint8_t i = offset; i + 1u < count; ++i) {
        const uint8_t dst = (uint8_t)((m->head + i) % HARDRT_APP_MAX_TASKS);
        const uint8_t src = (uint8_t)((m->head + i + 1u) % HARDRT_APP_MAX_TASKS);
        m->q[dst] = m->q[src];
    }
    m->tail = (uint8_t)((m->tail + HARDRT_APP_MAX_TASKS - 1u) %
                        HARDRT_APP_MAX_TASKS);
    m->count_wait--;
    return 0;
}

static int can_own_locked(int id) {
    return id >= 0 && id < HARDRT_APP_MAX_TASKS &&
           hrt__tcb(id)->held_mutex_count < HARDRT_MAX_MUTEXES_HELD;
}

static void attach_locked(int id, hrt_mutex_t *m) {
    _hrt_tcb_t *const task = hrt__tcb(id);
    task->held_mutexes[task->held_mutex_count++] = m;
}

static int detach_locked(int id, const hrt_mutex_t *m) {
    _hrt_tcb_t *const task = hrt__tcb(id);
    for (uint8_t i = 0u; i < task->held_mutex_count; ++i) {
        if (task->held_mutexes[i] != m) continue;
        for (uint8_t j = i; j + 1u < task->held_mutex_count; ++j) {
            task->held_mutexes[j] = task->held_mutexes[j + 1u];
        }
        task->held_mutexes[--task->held_mutex_count] = NULL;
        return 0;
    }
    hrt_error(ERR_INVALID_STATE);
    return -1;
}

/* Scan only the statically bounded mutexes held by this owner and their
 * bounded waiter rings. A waiter's *effective* priority donates transitively. */
static uint8_t desired_priority_locked(int owner) {
    const _hrt_tcb_t *const t = hrt__tcb(owner);
    uint8_t best = t->base_prio;
    for (uint8_t i = 0u; i < t->held_mutex_count; ++i) {
        const hrt_mutex_t *const m = t->held_mutexes[i];
        for (uint8_t j = 0u; j < m->count_wait; ++j) {
            const int waiter = m->q[(m->head + j) % HARDRT_APP_MAX_TASKS];
            if (waiter < 0 || waiter >= HARDRT_APP_MAX_TASKS) {
                hrt_error(ERR_INVALID_STATE);
                continue;
            }
            const uint8_t urgency = hrt__tcb(waiter)->prio;
            if (urgency < best) best = urgency;
        }
    }
    return best;
}

/* Bounded owner-chain propagation. A blocked task can wait on exactly one
 * mutex, and cycle prevention keeps this chain finite (at most N owners). */
static void propagate_locked(int owner) {
    for (int depth = 0; depth < HARDRT_APP_MAX_TASKS; ++depth) {
        if (owner < 0 || owner >= HARDRT_APP_MAX_TASKS) return;
        _hrt_tcb_t *const t = hrt__tcb(owner);
        const uint8_t desired = desired_priority_locked(owner);
        if (desired == t->prio) return;
        hrt__pi_set_effective_priority_locked(owner, desired);
        if (t->waiting_mutex == NULL) return;
        owner = t->waiting_mutex->owner;
    }
    hrt_error(ERR_INVALID_STATE);
}

static int would_cycle_locked(int waiter, const hrt_mutex_t *target) {
    const hrt_mutex_t *cursor = target;
    for (int depth = 0; depth < HARDRT_APP_MAX_TASKS; ++depth) {
        if (cursor == NULL || !cursor->locked) return 0;
        const int owner = cursor->owner;
        if (owner == waiter) return 1;
        if (owner < 0 || owner >= HARDRT_APP_MAX_TASKS) return 1;
        cursor = hrt__tcb(owner)->waiting_mutex;
    }
    return 1;
}

static int prepare_wait_locked(hrt_mutex_t *m, int me) {
    if (!can_own_locked(me) || would_cycle_locked(me, m)) {
        hrt_error(ERR_INVALID_STATE);
        return -1;
    }
    return 0;
}

/* Called inside hrt__sleep_tick() while holding the same kernel lock used
 * for owner unlock. Expiry unlinks first, withdraws donation, then the
 * shared timeout engine publishes the expired task READY exactly once. */
static int timeout_cancel(void *context, int task_id) {
    hrt_mutex_t *const m = (hrt_mutex_t *)context;
    if (m == NULL || task_id < 0 || task_id >= HARDRT_APP_MAX_TASKS ||
        hrt__tcb(task_id)->waiting_mutex != m ||
        waitq_remove(m, (uint8_t)task_id) != 0) {
        hrt_error(ERR_INVALID_STATE);
        return -1;
    }
    hrt__tcb(task_id)->waiting_mutex = NULL;
    propagate_locked(m->owner);
    return 0;
}

int hrt_mutex_try_lock(hrt_mutex_t *m) {
    if (m == NULL) return -1;
    const int me = hrt__current_running_app_task();
    if (me < 0) {
        hrt_error(ERR_MUTEX_BAD_CTX);
        return -1;
    }

    hrt_port_crit_enter();
    int result = -1;
    if (!m->locked && can_own_locked(me)) {
        m->locked = 1u;
        m->owner = (int16_t)me;
        attach_locked(me, m);
        result = 0;
    } else if (m->locked && m->owner == me) {
        hrt_error(ERR_MUTEX_RECURSIVE);
    } else if (!m->locked && !can_own_locked(me)) {
        hrt_error(ERR_INVALID_STATE);
    }
    hrt_port_crit_exit();
    return result;
}

int hrt_mutex_lock(hrt_mutex_t *m) {
    if (m == NULL) return -1;
    const int me = hrt__current_running_app_task();
    if (me < 0) return -1;

    hrt_port_crit_enter();
    if (!can_own_locked(me)) {
        hrt_error(ERR_INVALID_STATE);
        hrt_port_crit_exit();
        return -1;
    }
    if (!m->locked) {
        m->locked = 1u;
        m->owner = (int16_t)me;
        attach_locked(me, m);
        hrt_port_crit_exit();
        return 0;
    }
    if (m->owner == me) {
        hrt_error(ERR_MUTEX_RECURSIVE);
        hrt_port_crit_exit();
        return -1;
    }
    _hrt_tcb_t *const t = hrt__tcb(me);
    if (t == NULL || t->waiting_mutex != NULL ||
        prepare_wait_locked(m, me) != 0 ||
        waitq_push(m, (uint8_t)me) != 0) {
        hrt_port_crit_exit();
        return -1;
    }

    t->waiting_mutex = m;
    t->state = HRT_BLOCKED;
    propagate_locked(m->owner);
    hrt__pend_context_switch();
    hrt_port_crit_exit();
    hrt_port_yield_to_scheduler();
    /* Handoff has already attached the mutex to this task. */
    return 0;
}

hrt_wait_result_t hrt_mutex_lock_until(hrt_mutex_t *m, hrt_tick_t deadline) {
    if (m == NULL) return HRT_WAIT_INVALID_ARGUMENT;
    const int me = hrt__current_running_app_task();
    if (me < 0) return HRT_WAIT_INVALID_CONTEXT;

    hrt_port_crit_enter();
    hrt_tick_t distance = 0u;
    const hrt_deadline_relation_t rel =
        hrt__deadline_relation(hrt_tick_now(), deadline, &distance);
    if (rel == HRT_DEADLINE_AMBIGUOUS) {
        hrt_port_crit_exit();
        return HRT_WAIT_INVALID_DEADLINE;
    }
    if (!can_own_locked(me)) {
        hrt_error(ERR_INVALID_STATE);
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }
    if (!m->locked) {
        m->locked = 1u;
        m->owner = (int16_t)me;
        attach_locked(me, m);
        hrt_port_crit_exit();
        return HRT_WAIT_OK;
    }
    if (m->owner == me) {
        hrt_error(ERR_MUTEX_RECURSIVE);
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }
    if (rel != HRT_DEADLINE_FUTURE) {
        hrt_port_crit_exit();
        return HRT_WAIT_TIMEOUT;
    }
    _hrt_tcb_t *const t = hrt__tcb(me);
    if (t == NULL || t->waiting_mutex != NULL ||
        prepare_wait_locked(m, me) != 0 ||
        waitq_push(m, (uint8_t)me) != 0) {
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }
    if (hrt__wait_timeout_arm_locked(me, distance, timeout_cancel, m) != 0) {
        (void)waitq_remove(m, (uint8_t)me);
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }

    t->waiting_mutex = m;
    t->state = HRT_BLOCKED;
    propagate_locked(m->owner);
    hrt__pend_context_switch();
    hrt_port_crit_exit();
    hrt_port_yield_to_scheduler();

    hrt_port_crit_enter();
    const hrt_wait_result_t result = hrt__wait_result_take_locked(me);
    hrt_port_crit_exit();
    return result;
}

int hrt_mutex_unlock(hrt_mutex_t *m) {
    if (m == NULL) return -1;
    const int me = hrt__current_running_app_task();
    if (me < 0) return -1;

    hrt_port_crit_enter();
    if (!m->locked || m->owner != me) {
        hrt_error(ERR_MUTEX_OWNER);
        hrt_port_crit_exit();
        return -1;
    }

    /* Validate the prospective handoff before detaching the old owner.
     * A corrupted queue must never leave a locked mutex without a tracker. */
    const int prospective = m->count_wait != 0u ? (int)m->q[m->head] : -1;
    if (prospective >= 0 &&
        (!can_own_locked(prospective) ||
         hrt__tcb(prospective)->waiting_mutex != m)) {
        hrt_error(ERR_INVALID_STATE);
        hrt_port_crit_exit();
        return -1;
    }
    if (detach_locked(me, m) != 0) {
        hrt_port_crit_exit();
        return -1;
    }

    const int waiter = waitq_pop(m);
    if (waiter >= 0) {
        /* Waiters are admitted only with spare ownership capacity;
         * a blocked task cannot acquire another mutex. */
        m->owner = (int16_t)waiter;
        attach_locked(waiter, m);
        hrt__tcb(waiter)->waiting_mutex = NULL;
        hrt__wait_satisfied_locked(waiter);
        propagate_locked(waiter);
        propagate_locked(me);
        hrt__make_ready(waiter);
    } else {
        m->locked = 0u;
        m->owner = HRT_MUTEX_NO_OWNER;
        propagate_locked(me);
    }

    const int should_switch = hrt__pi_should_reschedule_locked();
    if (should_switch != 0) hrt__pend_context_switch();
    hrt_port_crit_exit();
    if (should_switch != 0) hrt_port_yield_to_scheduler();
    return 0;
}
