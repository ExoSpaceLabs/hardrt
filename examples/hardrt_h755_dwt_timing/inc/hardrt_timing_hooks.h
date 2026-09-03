#ifndef HARDRT_DWT_TIMING_HOOKS_H
#define HARDRT_DWT_TIMING_HOOKS_H

#include <stdint.h>
#include "timing_shared.h"

/* Cortex-M7 DWT CYCCNT. The benchmark enables CYCCNT before the scheduler starts. */
#define HRT_BENCH_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

/*
 * IPC profile: measure only the semaphore ISR kernel path from entry into the
 * ISR-facing semaphore operation to the point immediately after its waiter has
 * been made READY. No task-resume/scheduler/context hooks are active in this
 * image. The end timestamp is captured before statistics bookkeeping.
 */
#define HRT_TIMING_ISR_IPC_ENTRY() \
    do { \
        g_timing_start_cycles = HRT_BENCH_DWT_CYCCNT; \
    } while (0)

#define HRT_TIMING_ISR_WAITER_READY(task_id) \
    do { \
        const uint32_t hrt_timing_end__ = HRT_BENCH_DWT_CYCCNT; \
        (void)(task_id); \
        hrt_timing_stats_record(&g_timing_stats, hrt_timing_end__ - g_timing_start_cycles); \
    } while (0)

/* Not part of the current IPC-ready metric. */
#define HRT_TIMING_ISR_IPC_EXIT() ((void)0)

#endif /* HARDRT_DWT_TIMING_HOOKS_H */
