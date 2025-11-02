#include "test_common.h"

/* Basic sanity: when no tasks are READY, the idle hook runs and scheduler remains live. */
static void long_sleeper(void *arg) {
    (void) arg;
    for (;;) { hrt_sleep(50); }
}

static void test_idle_counter_increments_when_idle(void) {
#ifdef HEARTOS_TEST_HOOKS
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (idle behavior)");

    static uint32_t st[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    int tid = hrt_create_task(long_sleeper, NULL, st, 1024, &a);
    T_ASSERT_TRUE(tid >= 0, "created long sleeper");

    /* Reset idle counter and run for a short period */
    hrt__test_idle_counter_reset();
    uint32_t start = hrt_tick_now();
    while (hrt_tick_now() - start < 10) {
    }

    /* Stop and yield back to scheduler */
    hrt__test_stop_scheduler();
    hrt_yield();

    unsigned long long idle_ticks = hrt__test_idle_counter_value();
    T_ASSERT_TRUE(idle_ticks > 0, "idle counter should increment when idle");
#else
    printf("SKIP: idle behavior test requires HEARTOS_TEST_HOOKS.\n");
#endif
}

static const test_case_t CASES[] = {
    {"Idle: idle hook is exercised when system idle", test_idle_counter_increments_when_idle},
};

const test_case_t *get_tests_idle_behavior(int *out_count) {
    if (out_count)*out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
