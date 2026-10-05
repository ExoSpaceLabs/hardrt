/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt.h"
#include "hardrt_queue.h"
#include "hardrt_port_int.h"
#include "hardrt_deadline.h"

#include <string.h>

static int _wq_push(uint8_t *qbuf, uint8_t *tail, uint8_t *count, const uint8_t id) {
    if (*count >= HARDRT_APP_MAX_TASKS) return -1;
    qbuf[*tail] = id;
    *tail = (uint8_t)((*tail + 1u) % HARDRT_APP_MAX_TASKS);
    (*count)++;
    return 0;
}

static int _wq_pop(uint8_t *qbuf, uint8_t *head, uint8_t *count) {
    if (!*count) return -1;
    const int id = qbuf[*head];
    *head = (uint8_t)((*head + 1u) % HARDRT_APP_MAX_TASKS);
    (*count)--;
    return id;
}

static int _wq_remove(uint8_t *qbuf,
                      const uint8_t head,
                      uint8_t *tail,
                      uint8_t *count,
                      const uint8_t id) {
    const uint8_t original_count = *count;
    uint8_t offset = 0u;

    while (offset < original_count) {
        const uint8_t index =
            (uint8_t)((head + offset) % HARDRT_APP_MAX_TASKS);
        if (qbuf[index] == id) break;
        offset++;
    }
    if (offset >= original_count) return -1;

    for (uint8_t i = offset; i + 1u < original_count; ++i) {
        const uint8_t dst =
            (uint8_t)((head + i) % HARDRT_APP_MAX_TASKS);
        const uint8_t src =
            (uint8_t)((head + i + 1u) % HARDRT_APP_MAX_TASKS);
        qbuf[dst] = qbuf[src];
    }

    *tail = (uint8_t)((*tail + HARDRT_APP_MAX_TASKS - 1u) %
                      HARDRT_APP_MAX_TASKS);
    (*count)--;
    return 0;
}

static int _timeout_cancel_rx(void *context, const int task_id) {
    hrt_queue_t *const q = (hrt_queue_t *)context;
    if (q == NULL || task_id < 0 || task_id >= HARDRT_APP_MAX_TASKS) return -1;
    return _wq_remove(q->rx_q, q->rx_head, &q->rx_tail, &q->rx_wait,
                      (uint8_t)task_id);
}

static int _timeout_cancel_tx(void *context, const int task_id) {
    hrt_queue_t *const q = (hrt_queue_t *)context;
    if (q == NULL || task_id < 0 || task_id >= HARDRT_APP_MAX_TASKS) return -1;
    return _wq_remove(q->tx_q, q->tx_head, &q->tx_tail, &q->tx_wait,
                      (uint8_t)task_id);
}

/* Caller holds the kernel critical section. Freeze the scheduling decision in
 * the same protected state transition that publishes the waiter as READY. */
static int _wake_waiter_locked(const int waiter) {
    if (waiter < 0) return 0;
    hrt__wait_satisfied_locked(waiter);
    hrt__make_ready(waiter);
    return hrt__should_preempt_after_wake(waiter);
}

static void _preempt_task_after_wake(const int should_switch) {
    if (should_switch) {
        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }
}

void hrt_queue_init(hrt_queue_t *q, void *storage, uint16_t capacity, size_t item_size) {
    HRT_ASSERT(q);
    HRT_ASSERT(storage);
    HRT_ASSERT(capacity > 0);
    HRT_ASSERT(item_size > 0);

    q->buf = (uint8_t *)storage;
    q->item_size = item_size;
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;
    q->rx_head = q->rx_tail = q->rx_wait = 0;
    q->tx_head = q->tx_tail = q->tx_wait = 0;
}

static int _enqueue_cs(hrt_queue_t *q, const void *item) {
    if (q->count >= q->capacity) return -1;
    const uint16_t idx = q->tail;
    memcpy(&q->buf[(size_t)idx * q->item_size], item, q->item_size);
    q->tail = (uint16_t)((q->tail + 1u) % q->capacity);
    q->count++;
    return 0;
}

static int _dequeue_cs(hrt_queue_t *q, void *out) {
    if (!q->count) return -1;
    const uint16_t idx = q->head;
    memcpy(out, &q->buf[(size_t)idx * q->item_size], q->item_size);
    q->head = (uint16_t)((q->head + 1u) % q->capacity);
    q->count--;
    return 0;
}

int hrt_queue_try_send(hrt_queue_t *q, const void *item) {
    HRT_ASSERT(q);
    HRT_ASSERT(item);
    int ok;
    int should_switch = 0;

    hrt_port_crit_enter();
    ok = _enqueue_cs(q, item);
    if (ok == 0) {
        const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
        should_switch = _wake_waiter_locked(waiter);
    }
    hrt_port_crit_exit();

    _preempt_task_after_wake(should_switch);
    return ok;
}

int hrt_queue_try_send_from_isr(hrt_queue_t *q, const void *item, int *need_switch) {
    HRT_ASSERT(q);
    HRT_ASSERT(item);
    int ok;
    int should_switch = 0;

    hrt_port_crit_enter();
    ok = _enqueue_cs(q, item);
    if (ok == 0) {
        const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
        should_switch = _wake_waiter_locked(waiter);
    }
    hrt_port_crit_exit();

    if (need_switch) *need_switch = should_switch;
    if (should_switch) hrt__pend_context_switch();
    return ok;
}

int hrt_queue_send(hrt_queue_t *q, const void *item) {
    HRT_ASSERT(q);
    HRT_ASSERT(item);
    const int me = hrt__current_running_app_task();
    if (me < 0) return -1;
    for (;;) {
        if (hrt_queue_try_send(q, item) == 0) return 0;

        hrt_port_crit_enter();
        if (q->count < q->capacity) {
            const int ok = _enqueue_cs(q, item);
            const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
            const int should_switch = _wake_waiter_locked(waiter);
            hrt_port_crit_exit();
            _preempt_task_after_wake(should_switch);
            return ok;
        }

        if (_wq_push(q->tx_q, &q->tx_tail, &q->tx_wait, (uint8_t)me) != 0) {
            /* A task may become BLOCKED only after TX waiter membership exists. */
            hrt_port_crit_exit();
            return -1;
        }
        _hrt_tcb_t *t = hrt__tcb(me);
        if (t) t->state = HRT_BLOCKED;
        hrt_port_crit_exit();

        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }
}

hrt_wait_result_t hrt_queue_send_until(hrt_queue_t *q,
                                             const void *item,
                                             const hrt_tick_t deadline) {
    if (q == NULL || item == NULL) return HRT_WAIT_INVALID_ARGUMENT;

    const int me = hrt__current_running_app_task();
    if (me < 0) return HRT_WAIT_INVALID_CONTEXT;

    for (;;) {
        hrt_port_crit_enter();

        hrt_tick_t distance = 0u;
        const hrt_deadline_relation_t relation =
            hrt__deadline_relation(hrt_tick_now(), deadline, &distance);
        if (relation == HRT_DEADLINE_AMBIGUOUS) {
            hrt_port_crit_exit();
            return HRT_WAIT_INVALID_DEADLINE;
        }

        if (q->count < q->capacity) {
            const int ok = _enqueue_cs(q, item);
            const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
            const int should_switch = _wake_waiter_locked(waiter);
            hrt_port_crit_exit();
            _preempt_task_after_wake(should_switch);
            return ok == 0 ? HRT_WAIT_OK : HRT_WAIT_ERROR;
        }

        if (relation != HRT_DEADLINE_FUTURE) {
            hrt_port_crit_exit();
            return HRT_WAIT_TIMEOUT;
        }

        if (_wq_push(q->tx_q, &q->tx_tail, &q->tx_wait, (uint8_t)me) != 0) {
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }
        if (hrt__wait_timeout_arm_locked(me, distance, _timeout_cancel_tx, q) != 0) {
            (void)_wq_remove(q->tx_q, q->tx_head, &q->tx_tail, &q->tx_wait,
                             (uint8_t)me);
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }

        _hrt_tcb_t *const task = hrt__tcb(me);
        if (task == NULL) {
            (void)_wq_remove(q->tx_q, q->tx_head, &q->tx_tail, &q->tx_wait,
                             (uint8_t)me);
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }

        task->state = HRT_BLOCKED;
        hrt__pend_context_switch();
        hrt_port_crit_exit();
        hrt_port_yield_to_scheduler();

        hrt_port_crit_enter();
        const hrt_wait_result_t result = hrt__wait_result_take_locked(me);
        hrt_port_crit_exit();
        if (result != HRT_WAIT_OK) return result;

        /* A queue wake grants an opportunity, not ownership of a slot. If
         * another task barged before this task resumed, retry against the same
         * absolute deadline without rebasing it. */
    }
}

int hrt_queue_try_recv(hrt_queue_t *q, void *out) {
    HRT_ASSERT(q);
    HRT_ASSERT(out);
    int ok;
    int should_switch = 0;

    hrt_port_crit_enter();
    ok = _dequeue_cs(q, out);
    if (ok == 0) {
        const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
        should_switch = _wake_waiter_locked(waiter);
    }
    hrt_port_crit_exit();

    _preempt_task_after_wake(should_switch);
    return ok;
}

int hrt_queue_try_recv_from_isr(hrt_queue_t *q, void *out, int *need_switch) {
    HRT_ASSERT(q);
    HRT_ASSERT(out);
    int ok;
    int should_switch = 0;

    hrt_port_crit_enter();
    ok = _dequeue_cs(q, out);
    if (ok == 0) {
        const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
        should_switch = _wake_waiter_locked(waiter);
    }
    hrt_port_crit_exit();

    if (need_switch) *need_switch = should_switch;
    if (should_switch) hrt__pend_context_switch();
    return ok;
}

int hrt_queue_recv(hrt_queue_t *q, void *out) {
    HRT_ASSERT(q);
    HRT_ASSERT(out);
    const int me = hrt__current_running_app_task();
    if (me < 0) return -1;
    for (;;) {
        if (hrt_queue_try_recv(q, out) == 0) return 0;

        hrt_port_crit_enter();
        if (q->count) {
            const int ok = _dequeue_cs(q, out);
            const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
            const int should_switch = _wake_waiter_locked(waiter);
            hrt_port_crit_exit();
            _preempt_task_after_wake(should_switch);
            return ok;
        }

        if (_wq_push(q->rx_q, &q->rx_tail, &q->rx_wait, (uint8_t)me) != 0) {
            /* A task may become BLOCKED only after RX waiter membership exists. */
            hrt_port_crit_exit();
            return -1;
        }
        _hrt_tcb_t *t = hrt__tcb(me);
        if (t) t->state = HRT_BLOCKED;
        hrt_port_crit_exit();

        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }
}

hrt_wait_result_t hrt_queue_recv_until(hrt_queue_t *q,
                                             void *out,
                                             const hrt_tick_t deadline) {
    if (q == NULL || out == NULL) return HRT_WAIT_INVALID_ARGUMENT;

    const int me = hrt__current_running_app_task();
    if (me < 0) return HRT_WAIT_INVALID_CONTEXT;

    for (;;) {
        hrt_port_crit_enter();

        hrt_tick_t distance = 0u;
        const hrt_deadline_relation_t relation =
            hrt__deadline_relation(hrt_tick_now(), deadline, &distance);
        if (relation == HRT_DEADLINE_AMBIGUOUS) {
            hrt_port_crit_exit();
            return HRT_WAIT_INVALID_DEADLINE;
        }

        if (q->count != 0u) {
            const int ok = _dequeue_cs(q, out);
            const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
            const int should_switch = _wake_waiter_locked(waiter);
            hrt_port_crit_exit();
            _preempt_task_after_wake(should_switch);
            return ok == 0 ? HRT_WAIT_OK : HRT_WAIT_ERROR;
        }

        if (relation != HRT_DEADLINE_FUTURE) {
            hrt_port_crit_exit();
            return HRT_WAIT_TIMEOUT;
        }

        if (_wq_push(q->rx_q, &q->rx_tail, &q->rx_wait, (uint8_t)me) != 0) {
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }
        if (hrt__wait_timeout_arm_locked(me, distance, _timeout_cancel_rx, q) != 0) {
            (void)_wq_remove(q->rx_q, q->rx_head, &q->rx_tail, &q->rx_wait,
                             (uint8_t)me);
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }

        _hrt_tcb_t *const task = hrt__tcb(me);
        if (task == NULL) {
            (void)_wq_remove(q->rx_q, q->rx_head, &q->rx_tail, &q->rx_wait,
                             (uint8_t)me);
            hrt_port_crit_exit();
            return HRT_WAIT_ERROR;
        }

        task->state = HRT_BLOCKED;
        hrt__pend_context_switch();
        hrt_port_crit_exit();
        hrt_port_yield_to_scheduler();

        hrt_port_crit_enter();
        const hrt_wait_result_t result = hrt__wait_result_take_locked(me);
        hrt_port_crit_exit();
        if (result != HRT_WAIT_OK) return result;
    }
}
