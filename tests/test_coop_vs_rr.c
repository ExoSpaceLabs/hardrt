/* Ensures a cooperative task (no yield) starves an RR peer at the same priority
 * until it voluntarily sleeps; after a sleep, RR peer must make progress. */
#include "test_common.h"

static volatile int g_coopA_iters = 0;
static volatile int g_rrB_iters = 0;
static volatile int g_coopA_slept = 0;
static volatile int g_rrB_before_sleep = 0;

static void coop_task_A(void *arg) {
    (void) arg;
    for (;;) {
        ++g_coopA_iters;
        if (g_coopA_iters >= 50000) {
            /* finally allow others */
            g_coopA_slept = 1;
            hrt_sleep(1);
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        /* no yield here to simulate cooperative non-preemptible work */
    }
}

static void rr_task_B(void *arg) {
    (void) arg;
    for (;;) {
        if (!g_coopA_slept) {
            ++g_rrB_before_sleep;
        }
        ++g_rrB_iters;
        hrt_sleep(1);
    }
}

static void test_cooperative_vs_rr_same_priority(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (coop vs RR)");

    static uint32_t sa[2048], sb[2048];
    hrt_task_attr_t coop = {.priority = HRT_PRIO1, .timeslice = 0};
    hrt_task_attr_t rr = {.priority = HRT_PRIO1, .timeslice = 3};
    int a = hrt_create_task(coop_task_A, NULL, sa, sizeof(sa) / sizeof(sa[0]), &coop);
    int b = hrt_create_task(rr_task_B, NULL, sb, sizeof(sb) / sizeof(sb[0]), &rr);
    T_ASSERT_TRUE(a >= 0 && b >= 0, "created cooperative and RR tasks");

    g_coopA_iters = g_rrB_iters = 0;
    g_coopA_slept = 0;
    g_rrB_before_sleep = 0;
    hrt_start();

    /* RR task must not run while cooperative A did not yield/sleep */
    T_ASSERT_EQ_INT(0, g_rrB_before_sleep, "RR task should be starved by cooperative peer until A sleeps");
    T_ASSERT_TRUE(g_coopA_iters >= 50000, "cooperative A executed expected work before sleeping");
    /* After A slept once, B should have made progress */
    T_ASSERT_TRUE(g_rrB_iters >= 1, "RR task should run after cooperative peer sleeps");
}

static const test_case_t CASES[] = {
    {"Cooperative vs RR mix (same priority)", test_cooperative_vs_rr_same_priority},
};

const test_case_t *get_tests_coop_vs_rr(int *out_count) {
    if (out_count) *out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
