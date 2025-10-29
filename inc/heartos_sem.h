#ifndef HEARTOS_SEM_H
#define HEARTOS_SEM_H
#ifdef __cplusplus
extern "C" {
#endif
    /* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include "heartos_cfg.h"
#include "heartos.h"

    /* Binary semaphore.
     * count is 0 or 1. Waiters are queued FIFO (per-priority RR comes from the core).
     */
    typedef struct {
        volatile uint8_t count;           /* 0 or 1 */
        uint8_t          q[HEARTOS_MAX_TASKS];
        uint8_t          head, tail, count_wait;
    } hrt_sem_t;

    /* Initialize with initial count: 0 (empty) or 1 (available). */
    static inline void hrt_sem_init(hrt_sem_t* s, unsigned init)
    {
        s->count = (init ? 1u : 0u);
        s->head = s->tail = s->count_wait = 0;
    }

    /* Take the semaphore, blocking until available. Returns 0 on success. */
    int hrt_sem_take(hrt_sem_t* s);

    /* Try to take without blocking. Returns 0 on success, -1 if not available. */
    int hrt_sem_try_take(hrt_sem_t* s);

    /* Give (release) from task context. Returns 0. */
    int hrt_sem_give(hrt_sem_t* s);

    /* Give from ISR/tick context. Sets *need_switch=1 if a waiter was woken. */
    int hrt_sem_give_from_isr(hrt_sem_t* s, int* need_switch);



#ifdef __cplusplus
}
#endif
#endif