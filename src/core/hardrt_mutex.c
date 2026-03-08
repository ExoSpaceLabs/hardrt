/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_mutex.h"
#include "hardrt.h"
#include "hardrt_cfg.h"

/* Core-private hooks */
int hrt__get_current(void);
void hrt__make_ready(int id);
extern _hrt_tcb_t *hrt__tcb(int id);
void hrt__pend_context_switch(void);

/* Port-provided critical section */
void hrt_port_crit_enter(void);
void hrt_port_crit_exit(void);

/* Port-provided yield trampoline */
extern void hrt_port_yield_to_scheduler(void);

static void _waitq_push(hrt_mutex_t *m, uint8_t id) {
    if (m->count_wait >= HARDRT_MAX_TASKS) return;
    m->q[m->tail] = id;
    m->tail = (uint8_t)((m->tail + 1u) % HARDRT_MAX_TASKS);
    m->count_wait++;
}

static int _waitq_pop(hrt_mutex_t *m) {
    if (!m->count_wait) return -1;
    const int id = m->q[m->head];
    m->head = (uint8_t)((m->head + 1u) % HARDRT_MAX_TASKS);
    m->count_wait--;
    return id;
}

/*
 * Attempt to acquire the mutex without blocking.
 *
 * Special case: allow locking from main() or similar contexts (me == -1).
 */
int hrt_mutex_try_lock(hrt_mutex_t *m) {
    int me = hrt__get_current();
    /* If scheduler not yet started (me < 0), we treat it as a special case:
     * we allow locking from main() or similar contexts.
     * We'll use a special ID like -1 to represent 'outside task context'.
     */
    if (me < -1 || me >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_MUTEX_BAD_CTX);
        return -1;
    }

    hrt_port_crit_enter();

    if (!m->locked) {
        m->locked = 1u;
        m->owner = (int8_t)me;
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

/*
 * Block until the mutex is acquired.
 *
 * MUST be called from a task context (me >= 0).
 */
int hrt_mutex_lock(hrt_mutex_t *m) {
    int me = hrt__get_current();
    if (me < 0 || me >= HARDRT_MAX_TASKS) {
        hrt_error(ERR_MUTEX_BAD_CTX);
        return -1;
    }

    /* Fast path */
    if (hrt_mutex_try_lock(m) == 0) return 0;

    hrt_port_crit_enter();

    /* Re-check under CS */
    if (!m->locked) {
        m->locked = 1u;
        m->owner = (int8_t)me;
        hrt_port_crit_exit();
        return 0;
    }

    if (m->owner == me) {
        hrt_port_crit_exit();
        hrt_error(ERR_MUTEX_RECURSIVE);
        return -1;
    }

    _waitq_push(m, (uint8_t)me);

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

    /* When resumed, ownership was transferred to us by unlock() */
    return 0;
}

/*
 * Release a mutex held by the current caller.
 *
 * Can be called from outside a task context if previously locked there.
 */
int hrt_mutex_unlock(hrt_mutex_t *m) {
    int me = hrt__get_current();
    if (me < -1 || me >= HARDRT_MAX_TASKS) {
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
        /* Direct handoff: mutex stays locked, ownership moves to waiter */
        m->locked = 1u;
        m->owner = (int8_t)waiter;
        hrt__make_ready(waiter);
        hrt_port_crit_exit();

        /* Let awakened waiter run promptly */
        hrt_yield();
        return 0;
    }

    /* Nobody waiting: release */
    m->locked = 0u;
    m->owner = -1;
    hrt_port_crit_exit();
    return 0;
}