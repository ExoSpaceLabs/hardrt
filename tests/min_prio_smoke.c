/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

#include "hardrt.h"

#if HARDRT_MAX_PRIO != 1
#error "min_prio_smoke must be built with HARDRT_MAX_PRIO=1"
#endif

static void dummy_task(void *arg) {
    (void)arg;
    for (;;) hrt_yield();
}

int main(void) {
    hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 3u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_EXTERNAL,
    };
    if (hrt_init(&cfg) != 0) return 1;

    static uint32_t bad_stack[256];
    static uint32_t good_stack[256];

    hrt_task_attr_t invalid = {
        .priority = HRT_PRIO1,
        .timeslice = 3u,
    };
    if (hrt_create_task(dummy_task, 0, bad_stack, 256u, &invalid) >= 0) return 2;

    /* attr==NULL must choose priority 0 in a one-priority build. */
    if (hrt_create_task(dummy_task, 0, good_stack, 256u, 0) != 0) return 3;

    return 0;
}
