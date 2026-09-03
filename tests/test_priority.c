/* Tests strict priority dominance: with PRIORITY policy, lower-priority tasks
 * must not run while a higher-priority READY task keeps yielding. */
#include "test_common.h"

static volatile int g_prio_high_iters = 0;
static volatile int g_prio_low_iters = 0;
static volatile int g_prio_high_slept = 0; /* flag set by high when it blocks */
static volatile int g_prio_low_before_sleep = 0; /* low-prio runs counted before high-ever-blocked */

static void prio_high_task(void *arg) {
    (void) arg;
    for (;;) {
        ++g_prio_high_iters;
        /* Keep yielding to re-enter scheduler; with PRIORITY policy, low prio must not run. */
        if (g_prio_high_iters >= 2000) {
            /* Block for a deliberately long interval. The low-priority task
             * stops the test as soon as it is dispatched, so this does not
             * add wall-clock delay and cannot race a one-tick wakeup. */
            g_prio_high_slept = 1;
            hrt_sleep(1000);
            /* Reaching here would mean the lower task failed to stop the test. */
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_yield();
    }
}

static void prio_low_task(void *arg) {
    (void) arg;
    for (;;) {
        if (!g_prio_high_slept) {
            ++g_prio_low_before_sleep;
        }
        ++g_prio_low_iters;

        if (g_prio_high_slept) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }

        hrt_yield();
    }
}

static void test_priority_dominance_priority_policy(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY, .default_slice = 0};
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (PRIORITY policy)");

    static uint32_t sh[2048], sl[2048];
    hrt_task_attr_t ah = {.priority = HRT_PRIO0, .timeslice = 0}; /* cooperative high */
    hrt_task_attr_t al = {.priority = HRT_PRIO1, .timeslice = 0};
    int th = hrt_create_task(prio_high_task, NULL, sh, sizeof(sh) / sizeof(sh[0]), &ah);
    int tl = hrt_create_task(prio_low_task, NULL, sl, sizeof(sl) / sizeof(sl[0]), &al);
    T_ASSERT_TRUE(th >= 0 && tl >= 0, "created high and low priority tasks");

    g_prio_high_iters = g_prio_low_iters = 0;
    g_prio_high_slept = 0;
    g_prio_low_before_sleep = 0;
    hrt_start();

    T_ASSERT_EQ_INT(0, g_prio_low_before_sleep, "low prio should not run while high prio remained READY");
    T_ASSERT_TRUE(g_prio_high_iters >= 2000, "high prio performed expected iterations");
    T_ASSERT_TRUE(g_prio_low_iters >= 1, "low prio should run after high prio blocks");
}

static const test_case_t CASES[] = {
    {"Strict priority dominance (PRIORITY policy)", test_priority_dominance_priority_policy},
};

const test_case_t *get_tests_priority(int *out_count) {
    if (out_count) *out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
