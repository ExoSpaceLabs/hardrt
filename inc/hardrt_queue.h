/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HARDRT_QUEUE_H
#define HARDRT_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "hardrt_cfg.h"
#include "hardrt.h"

/**
 * @brief Fixed-size message queue (ring buffer) for inter-task communication.
 *
 * Notes (because someone has to write them):
 * - The queue copies items into an application-provided storage buffer.
 * - Keep item_size small. If you need to move big payloads, queue pointers or
 *   indices into a separate buffer pool.
 * - hrt_queue_send/recv block forever (no timeout yet), matching the current
 *   semaphore feature set.
 */

typedef struct {
    /* Application-provided backing store: capacity * item_size bytes */
    uint8_t *buf;
    size_t   item_size;
    uint16_t capacity;

    /* Ring state */
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;

    /* Receiver wait queue (FIFO) */
    uint8_t rx_q[HARDRT_MAX_TASKS];
    uint8_t rx_head, rx_tail, rx_wait;

    /* Sender wait queue (FIFO) */
    uint8_t tx_q[HARDRT_MAX_TASKS];
    uint8_t tx_head, tx_tail, tx_wait;
} hrt_queue_t;

/**
 * @brief Initialize a queue.
 *
 * @param q Queue object.
 * @param storage Byte buffer of size (capacity * item_size).
 * @param capacity Number of items the queue can hold (must be > 0).
 * @param item_size Size of each item in bytes (must be > 0).
 */
void hrt_queue_init(hrt_queue_t *q, void *storage, uint16_t capacity, size_t item_size);

/**
 * @brief Send (enqueue) an item, blocking until space is available.
 * @param q Queue.
 * @param item Pointer to item to copy into the queue.
 * @return 0 on success.
 */
int hrt_queue_send(hrt_queue_t *q, const void *item);

/**
 * @brief Try to send without blocking.
 * @return 0 on success, -1 if full.
 */
int hrt_queue_try_send(hrt_queue_t *q, const void *item);

/**
 * @brief Try to send from ISR context (non-blocking).
 * @param q Queue.
 * @param item Pointer to item to copy.
 * @param need_switch Optional out: set to 1 if a waiter was woken and a switch is advised.
 * @return 0 on success, -1 if full.
 */
int hrt_queue_try_send_from_isr(hrt_queue_t *q, const void *item, int *need_switch);

/**
 * @brief Receive (dequeue) an item, blocking until one is available.
 * @param q Queue.
 * @param out Pointer to storage where the received item will be copied.
 * @return 0 on success.
 */
int hrt_queue_recv(hrt_queue_t *q, void *out);

/**
 * @brief Try to receive without blocking.
 * @return 0 on success, -1 if empty.
 */
int hrt_queue_try_recv(hrt_queue_t *q, void *out);

/**
 * @brief Try to receive from ISR context (non-blocking).
 * @param q Queue.
 * @param out Destination buffer.
 * @param need_switch Optional out: set to 1 if a waiter was woken and a switch is advised.
 * @return 0 on success, -1 if empty.
 */
int hrt_queue_try_recv_from_isr(hrt_queue_t *q, void *out, int *need_switch);

/**
 * @brief Get current queue length (non-blocking).
 */
static inline uint16_t hrt_queue_count(const hrt_queue_t *q) {
    return (uint16_t)q->count;
}

#ifdef __cplusplus
}
#endif

#endif