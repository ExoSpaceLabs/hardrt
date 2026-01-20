/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_queue.h"

#include <string.h>

/* Core-private hooks (same pattern as hardrt_sem.c) */
int hrt__get_current(void);
void hrt__make_ready(int id);

/* Port-provided critical section (non-nestable minimal CS) */
void hrt_port_crit_enter(void);
void hrt_port_crit_exit(void);

/* Port-provided yield trampoline (task context) */
extern void hrt_port_yield_to_scheduler(void);

/* Core: request context switch at next safe point (PendSV on Cortex-M) */
void hrt__pend_context_switch(void);

/* ---------------- Internal waiter FIFO helpers ---------------- */
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

/* Enqueue common: expects CS held, returns 0 if enqueued, -1 if full */
static int _enqueue_cs(hrt_queue_t *q, const void *item) {
    if (q->count >= q->capacity) return -1;

    const uint16_t idx = q->tail;
    memcpy(&q->buf[(size_t)idx * q->item_size], item, q->item_size);
    q->tail = (uint16_t)((q->tail + 1u) % q->capacity);
    q->count++;
    return 0;
}

/* Dequeue common: expects CS held, returns 0 if dequeued, -1 if empty */
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
        /* If a receiver is waiting, wake exactly one. */
        const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
        if (waiter >= 0) {
            hrt__make_ready(waiter);
            woken = 1;
        }
    }
    hrt_port_crit_exit();

    /* Mirror semaphore behaviour: if we woke someone, yield to let it run. */
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
    if (woken) {
        hrt__pend_context_switch();
    }
    return ok;
}

int hrt_queue_send(hrt_queue_t *q, const void *item) {
    HRT_ASSERT(q);
    HRT_ASSERT(item);

    for (;;) {
        /* Fast path */
        if (hrt_queue_try_send(q, item) == 0) return 0;

        /* Full: block current task on TX waiters */
        const int me = hrt__get_current();

        hrt_port_crit_enter();

        /* Re-check after CS in case space appeared */
        if (q->count < q->capacity) {
            const int ok = _enqueue_cs(q, item);
            /* Potentially wake receiver */
            const int waiter = _wq_pop(q->rx_q, &q->rx_head, &q->rx_wait);
            if (waiter >= 0) hrt__make_ready(waiter);
            hrt_port_crit_exit();
            return ok;
        }

        /* Queue still full: park ourselves */
        _wq_push(q->tx_q, &q->tx_tail, &q->tx_wait, (uint8_t)me);
        _hrt_tcb_t *t = hrt__tcb(me);
        if (t) t->state = HRT_BLOCKED;
        hrt_port_crit_exit();

        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
        /* When resumed, loop and retry */
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
        /* If a sender is waiting, wake exactly one (we just freed space). */
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
    if (woken) {
        hrt__pend_context_switch();
    }
    return ok;
}

int hrt_queue_recv(hrt_queue_t *q, void *out) {
    HRT_ASSERT(q);
    HRT_ASSERT(out);

    for (;;) {
        /* Fast path */
        if (hrt_queue_try_recv(q, out) == 0) return 0;

        /* Empty: block current task on RX waiters */
        const int me = hrt__get_current();

        hrt_port_crit_enter();

        /* Re-check after CS in case data appeared */
        if (q->count) {
            const int ok = _dequeue_cs(q, out);
            /* Potentially wake sender */
            const int waiter = _wq_pop(q->tx_q, &q->tx_head, &q->tx_wait);
            if (waiter >= 0) hrt__make_ready(waiter);
            hrt_port_crit_exit();
            return ok;
        }

        /* Queue still empty: park ourselves */
        _wq_push(q->rx_q, &q->rx_tail, &q->rx_wait, (uint8_t)me);
        _hrt_tcb_t *t = hrt__tcb(me);
        if (t) t->state = HRT_BLOCKED;
        hrt_port_crit_exit();

        hrt__pend_context_switch();
        hrt_port_yield_to_scheduler();
    }
}
