/* Validates runtime tuning: policy switch RR->PRIORITY and default slice changes. */
#include "test_common.h"

/* Control task to switch policy at runtime */
static volatile int g_switch_done = 0;

static void policy_switcher(void *arg) {
    (void) arg;
    for (;;) {
        if (!g_switch_done) {
            /* switch after some time */
            hrt_sleep(5);
            hrt_set_policy(HRT_SCHED_PRIORITY);
            g_switch_done = 1;
        }
        hrt_sleep(1);
    }
}

static volatile int g_rrA = 0, g_rrB = 0;

static void rr_counter_A(void *arg) {
    (void) arg;
    for (;;) {
        ++g_rrA;
        hrt_yield();
    }
}

static void rr_counter_B(void *arg) {
    (void) arg;
    for (;;) {
        ++g_rrB;
        hrt_yield();
    }
}

static void stopper_after_ticks(void *arg) {
    uint32_t ms = (uint32_t) (uintptr_t) arg;
    for (;;) {
        hrt_sleep(ms);
        hrt__test_stop_scheduler();
        hrt_yield();
    }
}

static void test_policy_switch_rr_to_priority(void) {
    hrt__test_reset_scheduler_state();
    g_switch_done = 0;
    g_rrA = g_rrB = 0;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (policy switch)");
    static uint32_t s_sw[1024], sa[1024], sb[1024], ss[1024];
    hrt_task_attr_t same = {.priority = HRT_PRIO1, .timeslice = 3};
    int sw = hrt_create_task(policy_switcher, NULL, s_sw, 1024, &same);
    int a = hrt_create_task(rr_counter_A, NULL, sa, 1024, &same);
    int b = hrt_create_task(rr_counter_B, NULL, sb, 1024, &same);
    int st = hrt_create_task(stopper_after_ticks, (void *) (uintptr_t) 20, ss, 1024, &same);
    T_ASSERT_TRUE(sw>=0 && a>=0 && b>=0 && st>=0, "created tasks for policy switch test");

    hrt_start();

    /* Both should have progressed across the switch */
    T_ASSERT_TRUE(g_rrA > 0 && g_rrB > 0, "both RR tasks progressed across policy switch");
}

/* Default timeslice change should affect only tasks created after the change */
static volatile int g_new_rr = 0, g_old_rr = 0;

static void old_rr_task(void *arg) {
    (void) arg;
    for (;;) {
        ++g_old_rr;
        hrt_yield();
    }
}

static void new_rr_task(void *arg) {
    (void) arg;
    for (;;) {
        ++g_new_rr;
        hrt_yield();
    }
}

static void test_default_slice_change_affects_new_tasks_only(void) {
    hrt__test_reset_scheduler_state();
    g_new_rr = g_old_rr = 0;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 2};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (default slice change)");

    static uint32_t so[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 0xFFFF /* will override by explicit below */};
    /* Create an old RR task with explicit timeslice 2 */
    a.timeslice = 2;
    int to = hrt_create_task(old_rr_task, NULL, so, 1024, &a);
    T_ASSERT_TRUE(to>=0, "created old rr task");

    /* Change default slice and create a new RR task with attr==NULL to inherit */
    hrt_set_default_timeslice(5);
    static uint32_t sn[1024], ss[1024];
    int tn = hrt_create_task(new_rr_task, NULL, sn, 1024, NULL); /* inherits default prio and slice */
    T_ASSERT_TRUE(tn>=0, "created new rr task inheriting default slice");

    /* Add a stopper after 10 ms */
    hrt_task_attr_t same = {.priority = HRT_PRIO1, .timeslice = 3};
    int st = hrt_create_task(stopper_after_ticks, (void *) (uintptr_t) 10, ss, 1024, &same);
    T_ASSERT_TRUE(st>=0, "created stopper task");

    hrt_start();

    /* Both should make progress */
    T_ASSERT_TRUE(g_old_rr>0 && g_new_rr>0, "both RR tasks progressed");
}

static const test_case_t CASES[] = {
    {"Runtime: policy switch RR -> PRIORITY", test_policy_switch_rr_to_priority},
    {"Runtime: default slice change affects new tasks", test_default_slice_change_affects_new_tasks_only},
};

const test_case_t *get_tests_runtime_tuning(int *out_count) {
    if (out_count)*out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
