#ifndef HARDRT_DEADLINE_INTERNAL_H
#define HARDRT_DEADLINE_INTERNAL_H

#include "hardrt.h"

/*
 * Kernel-private serial-number arithmetic for the wrapping 32-bit tick domain.
 *
 * All consumers share this helper so periodic releases and IPC timeouts use
 * exactly the same half-range ordering contract. No signed overflow or
 * implementation-defined unsigned-to-signed conversion is required.
 */
typedef enum {
    HRT_DEADLINE_PAST = -1,
    HRT_DEADLINE_AT = 0,
    HRT_DEADLINE_FUTURE = 1,
    HRT_DEADLINE_AMBIGUOUS = 2
} hrt_deadline_relation_t;

static inline hrt_deadline_relation_t hrt__deadline_relation(
    const hrt_tick_t now,
    const hrt_tick_t deadline,
    hrt_tick_t *distance_ticks) {
    const hrt_tick_t forward = deadline - now;
    if (distance_ticks != NULL) *distance_ticks = 0u;

    if (forward == 0u) return HRT_DEADLINE_AT;
    if (forward == HRT_TICK_HALF_RANGE) return HRT_DEADLINE_AMBIGUOUS;

    if ((forward & HRT_TICK_HALF_RANGE) == 0u) {
        if (distance_ticks != NULL) *distance_ticks = forward;
        return HRT_DEADLINE_FUTURE;
    }

    if (distance_ticks != NULL) *distance_ticks = now - deadline;
    return HRT_DEADLINE_PAST;
}

#endif /* HARDRT_DEADLINE_INTERNAL_H */
