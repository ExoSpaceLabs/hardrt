#ifndef HEARTOS_SEM_H
#define HEARTOS_SEM_H
#ifdef __cplusplus
extern "C" {
#endif
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include "heartos_cfg.h"
#include "heartos.h"

/**
 * @brief Binary semaphore type (count is 0 or 1).
 * @details Waiters are queued FIFO; per-priority round-robin is handled by the core scheduler.
 */
typedef struct {
    volatile uint8_t count;           /**< 0 or 1 */
    uint8_t          q[HEARTOS_MAX_TASKS]; /**< Wait queue (task ids) */
    uint8_t          head, tail, count_wait; /**< Queue indices and length */
} hrt_sem_t;

/**
 * @brief Initialize a binary semaphore.
 * @param s Semaphore object to initialize.
 * @param init Initial count: 0 (empty) or 1 (available).
 */
static inline void hrt_sem_init(hrt_sem_t* s, unsigned init)
{
    s->count = (init ? 1u : 0u);
    s->head = s->tail = s->count_wait = 0;
}

/**
 * @brief Take the semaphore, blocking until available.
 * @param s Semaphore to take.
 * @return 0 on success.
 */
int hrt_sem_take(hrt_sem_t* s);

/**
 * @brief Try to take the semaphore without blocking.
 * @param s Semaphore to try take.
 * @return 0 on success, -1 if not available.
 */
int hrt_sem_try_take(hrt_sem_t* s);

/**
 * @brief Give (release) the semaphore from task context.
 * @param s Semaphore to release.
 * @return 0.
 */
int hrt_sem_give(hrt_sem_t* s);

/**
 * @brief Give (release) the semaphore from ISR/tick context.
 * @param s Semaphore to release.
 * @param need_switch Set to 1 if a higher-priority waiter was woken and a switch is needed.
 * @return 0.
 */
int hrt_sem_give_from_isr(hrt_sem_t* s, int* need_switch);

#ifdef __cplusplus
}
#endif
#endif