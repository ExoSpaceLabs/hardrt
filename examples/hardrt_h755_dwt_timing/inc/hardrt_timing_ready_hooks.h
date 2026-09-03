#ifndef HARDRT_DWT_READY_TIMING_HOOKS_H
#define HARDRT_DWT_READY_TIMING_HOOKS_H

#include <stdint.h>
#include "timing_shared.h"

/* Cortex-M7 DWT CYCCNT. The benchmark enables CYCCNT before the scheduler starts. */
#define HRT_BENCH_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

/*
 * READY-to-task profile: the semaphore ISR records only the instant at which
 * the blocked waiter has become READY. The awakened task records the end point
 * immediately after hrt_sem_take() returns.
 *
 * The resulting interval intentionally includes the remainder of the ISR-facing
 * semaphore path, critical-section exit, PendSV scheduling, context restore,
 * exception return, and the blocked API return path. It is therefore a dispatch
 * response metric, not a pure context-switch microbenchmark.
 */
#define HRT_TIMING_ISR_IPC_ENTRY() ((void)0)

#define HRT_TIMING_ISR_WAITER_READY(task_id) \
    do { \
        (void)(task_id); \
        g_timing_start_cycles = HRT_BENCH_DWT_CYCCNT; \
    } while (0)

#define HRT_TIMING_ISR_IPC_EXIT() ((void)0)

#endif /* HARDRT_DWT_READY_TIMING_HOOKS_H */