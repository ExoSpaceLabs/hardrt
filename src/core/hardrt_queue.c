/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt.h"
#include "hardrt_queue.h"
#include "hardrt_port_int.h"

#include <string.h>

static void _wq_push(uint8_t *qbuf, uint8_t *tail, uint8_t *count, const uint8_t id) {
    if (*count >= HARDRT_MAX_TASKS) return;
    qbuf[*tail] = id;
    *tail = (uint8_t)((*tail + 1u) % HARDRT_MAX_TASKS);
    (*count)++;
}

static int _wq_pop(uint8_t *qbuf, uint8_t *head, uint8_t *count) {
    if (!*count) return -1;
    const int id = qbuf[*head];
    *head = (uint8_t)((*head + 1u) % HARDRT_MAX_TASKS);
    (*count)--;
    return id;
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
    int woken = 0;

    hrt_port_crit_enter();
    ok = _enqueue_cs(q, item);
    if (ok == 0) {
        const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
        if (waiter >= 0) {
            hrt__make_ready(waiter);
            woken = 1;
        }
    }
    hrt_port_crit_exit();

    if (woken) hrt_yield();
    return ok;
}

int hrt_queue_try_send_from_isr(hrt_queue_t *q, const void *item, int *need_switch) {
    HRT_ASSERT(q);
    HRT_ASSERT(item);
    int ok;
    int woken = 0;

    hrt_port_crit_enter();
    ok = _enqueue_cs(q, item);
    if (ok == 0) {
        const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
        if (waiter >= 0) {
            hrt__make_ready(waiter);
            woken = 1;
        }
    }
    hrt_port_crit_exit();

    if (need_switch) *need_switch = woken;
    if (woken) hrt__pend_context_switch();
    return ok;
}

int hrt_queue_send(hrt_queue_t *q, const void *item) {
    HRT_ASSERT(q);
    HRT_ASSERT(item);
    for (;;) {
        if (hrt_queue_try_send(q, item) == 0) return 0;
        const int me = hrt__get_current();

        hrt_port_crit_enter();
        if (q->count < q->capacity) {
            const int ok = _enqueue_cs(q, item);
            const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
            if (waiter >= 0) hrt__make_ready(waiter);
            hrt_port_crit_exit();
            return ok;
        }

        _wq_push(q->tx_q, &q->tx_tail, &q->tx_wait, (uint8_t)me);
        _hrt_tcb_t *t = hrt__tcb(me);
        if (t) t->state = HRT_BLOCKED;
        hrt_port_crit_exit();

        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }
}

int hrt_queue_try_recv(hrt_queue_t *q, void *out) {
    HRT_ASSERT(q);
    HRT_ASSERT(out);
    int ok;
    int woken = 0;

    hrt_port_crit_enter();
    ok = _dequeue_cs(q, out);
    if (ok == 0) {
        const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
        if (waiter >= 0) {
            hrt__make_ready(waiter);
            woken = 1;
        }
    }
    hrt_port_crit_exit();

    if (woken) hrt_yield();
    return ok;
}

int hrt_queue_try_recv_from_isr(hrt_queue_t *q, void *out, int *need_switch) {
    HRT_ASSERT(q);
    HRT_ASSERT(out);
    int ok;
    int woken = 0;

    hrt_port_crit_enter();
    ok = _dequeue_cs(q, out);
    if (ok == 0) {
        const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
        if (waiter >= 0) {
            hrt__make_ready(waiter);
            woken = 1;
        }
    }
    hrt_port_crit_exit();

    if (need_switch) *need_switch = woken;
    if (woken) hrt__pend_context_switch();
    return ok;
}

int hrt_queue_recv(hrt_queue_t *q, void *out) {
    HRT_ASSERT(q);
    HRT_ASSERT(out);
    for (;;) {
        if (hrt_queue_try_recv(q, out) == 0) return 0;
        const int me = hrt__get_current();

        hrt_port_crit_enter();
        if (q->count) {
            const int ok = _dequeue_cs(q, out);
            const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
            if (waiter >= 0) hrt__make_ready(waiter);
            hrt_port_crit_exit();
            return ok;
        }

        _wq_push(q->rx_q, &q->rx_tail, &q->rx_wait, (uint8_t)me);
        _hrt_tcb_t *t = hrt__tcb(me);
        if (t) t->state = HRT_BLOCKED;
        hrt_port_crit_exit();

        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }
}
