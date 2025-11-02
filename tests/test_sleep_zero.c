#include "test_common.h"

/* Verify semantics of hrt_sleep(0): current implementation sleeps for at least 1 tick. */
static volatile int g_s0_elapsed = -1;

static void sleeper0(void *arg) {
    (void) arg;
    for (;;) {
        uint32_t start = hrt_tick_now();
        hrt_sleep(0);
        g_s0_elapsed = (int) (hrt_tick_now() - start);
        hrt__test_stop_scheduler();
        hrt_yield();
    }
}

static void test_sleep_zero_is_one_tick_minimum(void) {
    hrt__test_reset_scheduler_state();
    g_s0_elapsed = -1;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (sleep 0)");

    static uint32_t st[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    int tid = hrt_create_task(sleeper0, NULL, st, 1024, &a);
    T_ASSERT_TRUE(tid>=0, "created sleeper0 task");

    hrt_start();

    T_ASSERT_TRUE(g_s0_elapsed >= 1, "hrt_sleep(0) should delay by at least 1 tick");
}

static const test_case_t CASES[] = {
    {"Sleep: hrt_sleep(0) delays at least 1 tick", test_sleep_zero_is_one_tick_minimum},
};

const test_case_t *get_tests_sleep_zero(int *out_count) {
    if (out_count)*out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
