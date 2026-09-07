/* SPDX-License-Identifier: Apache-2.0 */
#include "test_common.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

static atomic_int g_async_high_ran;
static atomic_int g_async_low_started;
static atomic_int g_async_watchdog_fired;

static void async_high_task(void *arg) {
    (void)arg;
    hrt_sleep(10);
    atomic_store_explicit(&g_async_high_ran, 1, memory_order_release);
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void async_low_cpu_bound_task(void *arg) {
    (void)arg;
    atomic_store_explicit(&g_async_low_started, 1, memory_order_release);

    /* Deliberately no HardRT call here. The hosted port must interrupt this
     * task so tick processing can wake the higher-priority sleeper. */
    while (atomic_load_explicit(&g_async_high_ran, memory_order_acquire) == 0) {
        atomic_signal_fence(memory_order_seq_cst);
    }
}

static void *async_preemption_watchdog(void *arg) {
    (void)arg;
    const struct timespec one_ms = {0, 1000 * 1000};

    for (int i = 0; i < 1000; ++i) {
        if (atomic_load_explicit(&g_async_high_ran, memory_order_acquire) != 0) return NULL;
        (void)nanosleep(&one_ms, NULL);
    }

    atomic_store_explicit(&g_async_watchdog_fired, 1, memory_order_release);
    hrt__test_stop_scheduler();
    return NULL;
}

static void test_cpu_bound_task_is_asynchronously_preempted(void) {
    hrt__test_reset_scheduler_state();
    atomic_store_explicit(&g_async_high_ran, 0, memory_order_release);
    atomic_store_explicit(&g_async_low_started, 0, memory_order_release);
    atomic_store_explicit(&g_async_watchdog_fired, 0, memory_order_release);

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .tick_src = HRT_TICK_SYSTICK
    };
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "async preemption: init kernel");

    static uint32_t high_stack[1024];
    static uint32_t low_stack[1024];
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 0};
    const hrt_task_attr_t low = {.priority = HRT_PRIO1, .timeslice = 0};

    T_ASSERT_TRUE(hrt_create_task(async_high_task, NULL, high_stack, 1024, &high) >= 0,
                  "async preemption: created high sleeper");
    T_ASSERT_TRUE(hrt_create_task(async_low_cpu_bound_task, NULL, low_stack, 1024, &low) >= 0,
                  "async preemption: created CPU-bound low task");

    pthread_t watchdog;
    T_ASSERT_EQ_INT(0, pthread_create(&watchdog, NULL, async_preemption_watchdog, NULL),
                    "async preemption: created host watchdog");

    (void)hrt_start();
    (void)pthread_join(watchdog, NULL);

    T_ASSERT_EQ_INT(1, atomic_load_explicit(&g_async_low_started, memory_order_acquire),
                    "CPU-bound low task actually started");
    T_ASSERT_EQ_INT(1, atomic_load_explicit(&g_async_high_ran, memory_order_acquire),
                    "higher-priority sleeper woke despite CPU-bound low task");
    T_ASSERT_EQ_INT(0, atomic_load_explicit(&g_async_watchdog_fired, memory_order_acquire),
                    "host watchdog was not needed to escape cooperative starvation");
}

static const test_case_t CASES[] = {
    {"POSIX asynchronously preempts a CPU-bound task", test_cpu_bound_task_is_asynchronously_preempted},
};

const test_case_t *get_tests_posix_async_preemption(int *out_count) {
    if (out_count != NULL) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
