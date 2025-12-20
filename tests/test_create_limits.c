/* Verifies creation limits: max tasks enforced, min stack size rejected,
 * and default attributes inherited when attr==NULL. */
#include "test_common.h"

/* Helpers */
static void dummy_task(void *arg) {
    (void) arg;
    for (;;) { hrt_yield(); }
}

static void test_max_tasks_enforced(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (max tasks)");

    /* Create up to HARDRT_MAX_TASKS tasks successfully */
    int created = 0;
    static uint32_t stacks[HARDRT_MAX_TASKS][1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    for (int i = 0; i < HARDRT_MAX_TASKS; ++i) {
        int tid = hrt_create_task(dummy_task, NULL, stacks[i], 1024, &a);
        if (tid >= 0) ++created;
    }
    T_ASSERT_EQ_INT(HARDRT_MAX_TASKS, created, "should create up to HARDRT_MAX_TASKS tasks");

    /* Next creation should fail */
    static uint32_t extra[1024];
    int fail_tid = hrt_create_task(dummy_task, NULL, extra, 1024, &a);
    T_ASSERT_TRUE(fail_tid < 0, "creating beyond HARDRT_MAX_TASKS should fail");
}

static void test_min_stack_rejected(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (min stack)");

    uint32_t too_small[8]; /* < 64 words required by core guard */
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    int tid = hrt_create_task(dummy_task, NULL, too_small, sizeof(too_small) / sizeof(too_small[0]), &a);
    T_ASSERT_TRUE(tid < 0, "hrt_create_task should fail for stack smaller than 64 words");
}

/* Verify attr==NULL inherits cfg.default_slice = 0 (cooperative) */
static volatile int g_rr_iters = 0;
static volatile int g_rr_before_peer_sleep = 0;
static volatile int g_peer_slept = 0;

static void coop_peer(void *arg) {
    (void) arg;
    for (;;) {
        /* busy until we decide to sleep once */
        if (++g_peer_slept >= 50000) {
            g_peer_slept = 1;
            hrt_sleep(1);
            hrt__test_stop_scheduler();
            hrt_yield();
        }
    }
}

static void rr_peer(void *arg) {
    (void) arg;
    for (;;) {
        if (!g_peer_slept) ++g_rr_before_peer_sleep;
        ++g_rr_iters;
        hrt_sleep(1);
    }
}

static void test_attr_null_inherits_default_slice_zero(void) {
    hrt__test_reset_scheduler_state();
    g_rr_iters = 0;
    g_rr_before_peer_sleep = 0;
    g_peer_slept = 0;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 0};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (attr default)");

    static uint32_t sa[2048], sb[2048];
    /* A with attr==NULL should inherit default_slice=0 => cooperative */
    int a_id = hrt_create_task(coop_peer, NULL, sa, 2048, NULL);
    hrt_task_attr_t rr = {.priority = HRT_PRIO1, .timeslice = 3};
    int b_id = hrt_create_task(rr_peer, NULL, sb, 2048, &rr);
    T_ASSERT_TRUE(a_id >= 0 && b_id >= 0, "created tasks for default slice test");

    hrt_start();
    T_ASSERT_EQ_INT(0, g_rr_before_peer_sleep, "RR peer should be starved until coop (attr=NULL) sleeps");
    T_ASSERT_TRUE(g_rr_iters >= 1, "RR peer should run after coop peer sleeps once");
}

/* Sanity: configured limits should be consistent and dynamic. */
static void test_config_limits_sanity(void) {
    /* Macros come from public headers via compile definitions */
    T_ASSERT_TRUE(HARDRT_MAX_TASKS > 0, "HARDRT_MAX_TASKS must be > 0");
    T_ASSERT_TRUE(HARDRT_MAX_PRIO > 0, "HARDRT_MAX_PRIO must be > 0");
    T_ASSERT_TRUE(HARDRT_MAX_TASKS >= HARDRT_MAX_PRIO,
                  "Max tasks must be >= number of priority levels");
}

static const test_case_t CASES[] = {
    {"Config: limits sanity (tasks >= priorities)", test_config_limits_sanity},
    {"Create: max tasks enforcement", test_max_tasks_enforced},
    {"Create: minimum stack rejected", test_min_stack_rejected},
    {"Create: attr==NULL inherits default_slice=0 (cooperative)", test_attr_null_inherits_default_slice_zero},
};

const test_case_t *get_tests_create_limits(int *out_count) {
    if (out_count) *out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
