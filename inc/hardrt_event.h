/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HARDRT_EVENT_H
#define HARDRT_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "hardrt.h"

typedef uint32_t hrt_event_bits_t;

typedef enum {
    HRT_EVENT_WAIT_ANY = 0u,
    HRT_EVENT_WAIT_ALL = 1u << 0,
    HRT_EVENT_CLEAR_ON_EXIT = 1u << 1
} hrt_event_wait_option_t;

/**
 * @brief Statically allocated event-flag group.
 *
 * Waiter metadata is bounded by HARDRT_APP_MAX_TASKS. Applications should
 * initialize the object with hrt_event_init() before first use.
 */
typedef struct {
    volatile hrt_event_bits_t bits;
    uint8_t wait_q[HARDRT_APP_MAX_TASKS];
    uint8_t wait_count;
    uint8_t wait_active[HARDRT_APP_MAX_TASKS];
    uint8_t wait_options[HARDRT_APP_MAX_TASKS];
    hrt_event_bits_t wait_mask[HARDRT_APP_MAX_TASKS];
    hrt_event_bits_t wait_matched[HARDRT_APP_MAX_TASKS];
} hrt_event_t;

/** Initialize an event group with all bits clear and no waiters. */
void hrt_event_init(hrt_event_t *event);

/** Return a critical-section-consistent event-bit snapshot. */
hrt_event_bits_t hrt_event_get(const hrt_event_t *event);

/** OR bits into the group and wake every waiter satisfied by the update. */
int hrt_event_set(hrt_event_t *event, hrt_event_bits_t bits);

/** ISR-safe equivalent of hrt_event_set(). */
int hrt_event_set_from_isr(hrt_event_t *event,
                           hrt_event_bits_t bits,
                           int *need_switch);

/** Clear selected bits. Clearing never wakes waiters. */
int hrt_event_clear(hrt_event_t *event, hrt_event_bits_t bits);

/** ISR-safe bounded clear operation. It never requests a context switch. */
int hrt_event_clear_from_isr(hrt_event_t *event, hrt_event_bits_t bits);

/**
 * @brief Wait indefinitely for any or all bits in mask.
 *
 * A zero mask or unknown option bit is invalid. HRT_EVENT_WAIT_ANY is the
 * default. HRT_EVENT_WAIT_ALL requires every requested bit. If
 * HRT_EVENT_CLEAR_ON_EXIT is set, the matched bits are cleared after all
 * waiters satisfied by the same set operation have been evaluated against the
 * same event snapshot.
 *
 * @param event Event group.
 * @param mask Non-zero bit mask to wait for.
 * @param options HRT_EVENT_WAIT_ALL and/or HRT_EVENT_CLEAR_ON_EXIT.
 * @param matched Optional output receiving the matched bit subset.
 * @return 0 on satisfaction, -1 on invalid arguments/state.
 */
int hrt_event_wait(hrt_event_t *event,
                   hrt_event_bits_t mask,
                   unsigned options,
                   hrt_event_bits_t *matched);

#ifdef __cplusplus
}
#endif

#endif /* HARDRT_EVENT_H */
