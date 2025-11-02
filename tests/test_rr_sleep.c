/* Validates round-robin fairness when tasks perform short sleeps at same priority. */
#include "test_common.h"

static volatile int g_runs_a = 0, g_runs_b = 0;
static const int g_runs_target_each = 30;

static void rr_sleep_task_a(void *arg) {
    (void) arg;
    for (;;) {
        ++g_runs_a;
        if (g_runs_a >= g_runs_target_each && g_runs_b >= g_runs_target_each) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_sleep(1);
    }
}

static void rr_sleep_task_b(void *arg) {
    (void) arg;
    for (;;) {
        ++g_runs_b;
        if (g_runs_a >= g_runs_target_each && g_runs_b >= g_runs_target_each) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_sleep(1);
    }
}

static void test_rr_rotation_with_sleep_same_priority(void) {
    /* Two tasks at same priority with RR and short sleeps; time-slice expiry should rotate them over time. */
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (RR sleep test)");

    static uint32_t sa[2048], sb[2048];
    hrt_task_attr_t attr = {.priority = HRT_PRIO1, .timeslice = 3};
    int a = hrt_create_task(rr_sleep_task_a, NULL, sa, sizeof(sa) / sizeof(sa[0]), &attr);
    int b = hrt_create_task(rr_sleep_task_b, NULL, sb, sizeof(sb) / sizeof(sb[0]), &attr);
    T_ASSERT_TRUE(a >= 0 && b >= 0, "two RR tasks created (sleep test)");

    g_runs_a = g_runs_b = 0;
    hrt_start();

    T_ASSERT_TRUE(g_runs_a >= g_runs_target_each && g_runs_b >= g_runs_target_each,
                  "both RR-sleep tasks reached target iterations");
    int diff = g_runs_a - g_runs_b;
    if (diff < 0) diff = -diff;
    T_ASSERT_TRUE(diff <= g_runs_target_each/2, "RR with sleep should be reasonably balanced (diff <= 50%)");
}

static const test_case_t CASES[] = {
    {"RR rotation with sleep (same priority)", test_rr_rotation_with_sleep_same_priority},
};

const test_case_t *get_tests_rr_sleep(int *out_count) {
    if (out_count) *out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
