/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_mutex.h"
#include "hardrt_port_int.h"
#include "hardrt_deadline.h"

static int _waitq_push(hrt_mutex_t *m, uint8_t id) {
    if (m->count_wait >= HARDRT_APP_MAX_TASKS) return -1;
    m->q[m->tail] = id;
    m->tail = (uint8_t)((m->tail + 1u) % HARDRT_APP_MAX_TASKS);
    m->count_wait++;
    return 0;
}

static int _waitq_pop(hrt_mutex_t *m) {
    if (!m->count_wait) return -1;
    const int id = m->q[m->head];
    m->head = (uint8_t)((m->head + 1u) % HARDRT_APP_MAX_TASKS);
    m->count_wait--;
    return id;
}

static int _waitq_remove(hrt_mutex_t *m, const uint8_t id) {
    const uint8_t count = m->count_wait;
    uint8_t offset = 0u;

    while (offset < count) {
        const uint8_t index =
            (uint8_t)((m->head + offset) % HARDRT_APP_MAX_TASKS);
        if (m->q[index] == id) break;
        offset++;
    }
    if (offset >= count) return -1;

    for (uint8_t i = offset; i + 1u < count; ++i) {
        const uint8_t dst =
            (uint8_t)((m->head + i) % HARDRT_APP_MAX_TASKS);
        const uint8_t src =
            (uint8_t)((m->head + i + 1u) % HARDRT_APP_MAX_TASKS);
        m->q[dst] = m->q[src];
    }

    m->tail = (uint8_t)((m->tail + HARDRT_APP_MAX_TASKS - 1u) %
                        HARDRT_APP_MAX_TASKS);
    m->count_wait--;
    return 0;
}

static int _timeout_cancel(void *context, const int task_id) {
    hrt_mutex_t *const m = (hrt_mutex_t *)context;
    if (m == NULL || task_id < 0 || task_id >= HARDRT_APP_MAX_TASKS) return -1;
    return _waitq_remove(m, (uint8_t)task_id);
}

int hrt_mutex_try_lock(hrt_mutex_t *m) {
    const int me = hrt__get_current();
    if (me < 0 || me >= HARDRT_APP_MAX_TASKS) {
        hrt_error(ERR_MUTEX_BAD_CTX);
        return -1;
    }

    hrt_port_crit_enter();
    if (!m->locked) {
        m->locked = 1u;
        m->owner = me;
        hrt_port_crit_exit();
        return 0;
    }
    if (m->owner == me) {
        hrt_port_crit_exit();
        hrt_error(ERR_MUTEX_RECURSIVE);
        return -1;
    }
    hrt_port_crit_exit();
    return -1;
}

int hrt_mutex_lock(hrt_mutex_t *m) {
    const int me = hrt__get_current();
    if (me < 0 || me >= HARDRT_APP_MAX_TASKS) {
        hrt_error(ERR_MUTEX_BAD_CTX);
        return -1;
    }
    if (hrt_mutex_try_lock(m) == 0) return 0;

    hrt_port_crit_enter();
    if (!m->locked) {
        m->locked = 1u;
        m->owner = me;
        hrt_port_crit_exit();
        return 0;
    }
    if (m->owner == me) {
        hrt_port_crit_exit();
        hrt_error(ERR_MUTEX_RECURSIVE);
        return -1;
    }

    if (_waitq_push(m, (uint8_t)me) != 0) {
        /* Do not strand the caller as BLOCKED unless its waiter membership was
         * actually published. A full waiter queue here indicates inconsistent
         * kernel/IPC state rather than a normal resource-exhaustion case. */
        hrt_port_crit_exit();
        return -1;
    }
    _hrt_tcb_t *t = hrt__tcb(me);
    if (!t) {
        hrt_port_crit_exit();
        hrt_error(ERR_TCB_NULL);
        return -1;
    }
    t->state = HRT_BLOCKED;
    hrt_port_crit_exit();

    hrt__pend_context_switch();
    hrt_port_yield_to_scheduler();
    return 0;
}

hrt_wait_result_t hrt_mutex_lock_until(hrt_mutex_t *m,
                                       const hrt_tick_t deadline) {
    if (m == NULL) return HRT_WAIT_INVALID_ARGUMENT;

    const int me = hrt__current_running_app_task();
    if (me < 0) {
        hrt_error(ERR_MUTEX_BAD_CTX);
        return HRT_WAIT_INVALID_CONTEXT;
    }

    hrt_port_crit_enter();

    hrt_tick_t distance = 0u;
    const hrt_deadline_relation_t relation =
        hrt__deadline_relation(hrt_tick_now(), deadline, &distance);
    if (relation == HRT_DEADLINE_AMBIGUOUS) {
        hrt_port_crit_exit();
        return HRT_WAIT_INVALID_DEADLINE;
    }

    if (!m->locked) {
        m->locked = 1u;
        m->owner = me;
        hrt_port_crit_exit();
        return HRT_WAIT_OK;
    }

    if (m->owner == me) {
        hrt_port_crit_exit();
        hrt_error(ERR_MUTEX_RECURSIVE);
        return HRT_WAIT_ERROR;
    }

    if (relation != HRT_DEADLINE_FUTURE) {
        hrt_port_crit_exit();
        return HRT_WAIT_TIMEOUT;
    }

    if (_waitq_push(m, (uint8_t)me) != 0) {
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }
    if (hrt__wait_timeout_arm_locked(me, distance, _timeout_cancel, m) != 0) {
        (void)_waitq_remove(m, (uint8_t)me);
        hrt_port_crit_exit();
        return HRT_WAIT_ERROR;
    }

    _hrt_tcb_t *const task = hrt__tcb(me);
    if (task == NULL) {
        (void)_waitq_remove(m, (uint8_t)me);
        hrt_port_crit_exit();
        hrt_error(ERR_TCB_NULL);
        return HRT_WAIT_ERROR;
    }

    task->state = HRT_BLOCKED;
    hrt__pend_context_switch();
    hrt_port_crit_exit();
    hrt_port_yield_to_scheduler();

    hrt_port_crit_enter();
    const hrt_wait_result_t result = hrt__wait_result_take_locked(me);
    hrt_port_crit_exit();
    return result;
}

int hrt_mutex_unlock(hrt_mutex_t *m) {
    const int me = hrt__get_current();
    if (me < 0 || me >= HARDRT_APP_MAX_TASKS) {
        hrt_error(ERR_MUTEX_BAD_CTX);
        return -1;
    }

    hrt_port_crit_enter();
    if (!m->locked || m->owner != me) {
        hrt_port_crit_exit();
        hrt_error(ERR_MUTEX_OWNER);
        return -1;
    }

    const int waiter = _waitq_pop(m);
    if (waiter >= 0) {
        m->locked = 1u;
        m->owner = waiter;
        hrt__wait_satisfied_locked(waiter);
        hrt__make_ready(waiter);
        const int should_switch = hrt__should_preempt_after_wake(waiter);
        hrt_port_crit_exit();
        if (should_switch) {
            hrt__pend_context_switch();
            hrt_port_yield_to_scheduler();
        }
        return 0;
    }

    m->locked = 0u;
    m->owner = HRT_MUTEX_NO_OWNER;
    hrt_port_crit_exit();
    return 0;
}
