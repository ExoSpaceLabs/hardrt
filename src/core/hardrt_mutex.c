/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_mutex.h"
#include "hardrt_port_int.h"

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
