/* Scheduler-policy tests for priority RR and true global RR. */
#include "test_common.h"
#include "hardrt_sem.h"

#ifdef HARDRT_TEST_HOOKS
int hrt__test_ready_occurrences(int id);
#endif

/* ---- Existing PRIORITY_RR same-priority yield fairness ---- */
static volatile int g_yield_a = 0, g_yield_b = 0;
static volatile int g_yield_total = 0;
static const int g_yield_target = 200;

static void rr_yield_task_a(void *arg) {
    (void)arg;
    for (;;) {
        ++g_yield_a;
        ++g_yield_total;
        if (g_yield_total >= g_yield_target) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_yield();
    }
}

static void rr_yield_task_b(void *arg) {
    (void)arg;
    for (;;) {
        ++g_yield_b;
        ++g_yield_total;
        if (g_yield_total >= g_yield_target) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_yield();
    }
}

static void test_priority_rr_rotation_with_yield_same_priority(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 3
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (PRIORITY_RR yield test)");

    static uint32_t sa[2048], sb[2048];
    hrt_task_attr_t attr = {.priority = HRT_PRIO1, .timeslice = 3};
    int a = hrt_create_task(rr_yield_task_a, NULL, sa, sizeof(sa) / sizeof(sa[0]), &attr);
    int b = hrt_create_task(rr_yield_task_b, NULL, sb, sizeof(sb) / sizeof(sb[0]), &attr);
    T_ASSERT_TRUE(a >= 0 && b >= 0, "two PRIORITY_RR tasks created");

    g_yield_a = g_yield_b = g_yield_total = 0;
    hrt_start();

    T_ASSERT_TRUE(g_yield_a > 0 && g_yield_b > 0, "both PRIORITY_RR tasks made progress");
    int diff = g_yield_a - g_yield_b;
    if (diff < 0) diff = -diff;
    T_ASSERT_TRUE(diff <= g_yield_target / 5,
                  "PRIORITY_RR yield should distribute same-priority work fairly");
}

/* ---- Global RR ignores task priority ---- */
#define GLOBAL_SEQ_TARGET 12
static volatile int g_global_seq[GLOBAL_SEQ_TARGET];
static volatile int g_global_seq_pos;

static void global_rr_record(const int marker) {
    for (;;) {
        const int pos = g_global_seq_pos;
        if (pos < GLOBAL_SEQ_TARGET) {
            g_global_seq[pos] = marker;
            g_global_seq_pos = pos + 1;
        }
        if (g_global_seq_pos >= GLOBAL_SEQ_TARGET) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_yield();
    }
}

static void global_rr_a(void *arg) { (void)arg; global_rr_record(1); }
static void global_rr_b(void *arg) { (void)arg; global_rr_record(2); }
static void global_rr_c(void *arg) { (void)arg; global_rr_record(3); }

static void test_global_rr_ignores_priority(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_RR,
        .default_slice = 5
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (global RR priority independence)");

    static uint32_t sa[2048], sb[2048], sc[2048];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 5};
    hrt_task_attr_t low = {.priority = HRT_PRIO3, .timeslice = 5};
    hrt_task_attr_t mid = {.priority = HRT_PRIO1, .timeslice = 5};

    /* Deliberately create in an order that disagrees with priority order. */
    int a = hrt_create_task(global_rr_a, NULL, sa, 2048, &high);
    int b = hrt_create_task(global_rr_b, NULL, sb, 2048, &low);
    int c = hrt_create_task(global_rr_c, NULL, sc, 2048, &mid);
    T_ASSERT_TRUE(a >= 0 && b >= 0 && c >= 0, "created mixed-priority global RR tasks");

    memset((void *)g_global_seq, 0, sizeof(g_global_seq));
    g_global_seq_pos = 0;
    hrt_start();

    static const int expected[GLOBAL_SEQ_TARGET] = {
        1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3
    };
    T_ASSERT_EQ_INT(GLOBAL_SEQ_TARGET, g_global_seq_pos,
                    "global RR produced complete rotation trace");
    for (int i = 0; i < GLOBAL_SEQ_TARGET; ++i) {
        T_ASSERT_EQ_INT(expected[i], g_global_seq[i],
                        "global RR follows FIFO rotation independent of priority");
    }
}

/* ---- A wake joins global RR behind the current task ---- */
static hrt_sem_t g_global_wake_sem;
static volatile int g_global_wake_need_switch;
static volatile int g_global_giver_continued;
static volatile int g_global_waiter_observed_continue;

static void global_wake_waiter(void *arg) {
    (void)arg;
    hrt_sem_take(&g_global_wake_sem);
    g_global_waiter_observed_continue = g_global_giver_continued;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void global_wake_giver(void *arg) {
    (void)arg;
    int need = -1;
    hrt_sem_give_from_isr(&g_global_wake_sem, &need);
    g_global_wake_need_switch = need;
    g_global_giver_continued = 1;
    hrt_yield();
    for (;;) hrt_yield();
}

static void test_global_rr_wake_does_not_steal_quantum(void) {
    hrt__test_reset_scheduler_state();
    hrt_sem_init(&g_global_wake_sem, 0);
    g_global_wake_need_switch = -1;
    g_global_giver_continued = 0;
    g_global_waiter_observed_continue = 0;

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_RR,
        .default_slice = 10
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (global RR wake)");

    static uint32_t sw[2048], sg[2048];
    hrt_task_attr_t waiter_attr = {.priority = HRT_PRIO0, .timeslice = 10};
    hrt_task_attr_t giver_attr = {.priority = HRT_PRIO3, .timeslice = 10};

    int waiter = hrt_create_task(global_wake_waiter, NULL, sw, 2048, &waiter_attr);
    int giver = hrt_create_task(global_wake_giver, NULL, sg, 2048, &giver_attr);
    T_ASSERT_TRUE(waiter >= 0 && giver >= 0, "created global RR waiter and giver");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_global_wake_need_switch,
                    "global RR wake does not request immediate priority-style preemption");
    T_ASSERT_EQ_INT(1, g_global_giver_continued,
                    "current global RR task continues after waking peer");
    T_ASSERT_EQ_INT(1, g_global_waiter_observed_continue,
                    "woken task runs after current global RR task yields");
}

/* ---- Runtime switching rebuilds READY membership exactly once ---- */
static volatile int g_switch_controller_stage;
static volatile int g_switch_peer_a_runs;
static volatile int g_switch_peer_b_runs;
static volatile int g_switch_membership_ok;
static int g_switch_peer_a_id;
static int g_switch_peer_b_id;

static void policy_switch_peer_a(void *arg) {
    (void)arg;
    for (;;) {
        ++g_switch_peer_a_runs;
        hrt_yield();
    }
}

static void policy_switch_peer_b(void *arg) {
    (void)arg;
    for (;;) {
        ++g_switch_peer_b_runs;
        hrt_yield();
    }
}

static void policy_switch_controller(void *arg) {
    (void)arg;

    g_switch_controller_stage = 1;
    hrt_set_policy(HRT_SCHED_RR);

    /* Returning here proves the mixed-priority peers participated in global RR. */
    if (g_switch_peer_a_runs == 1 && g_switch_peer_b_runs == 1) {
        g_switch_controller_stage = 2;
    }

#ifdef HARDRT_TEST_HOOKS
    if (hrt__test_ready_occurrences(g_switch_peer_a_id) == 1 &&
        hrt__test_ready_occurrences(g_switch_peer_b_id) == 1) {
        g_switch_membership_ok++;
    }
#endif

    hrt_set_policy(HRT_SCHED_PRIORITY_RR);
#ifdef HARDRT_TEST_HOOKS
    if (hrt__test_ready_occurrences(g_switch_peer_a_id) == 1 &&
        hrt__test_ready_occurrences(g_switch_peer_b_id) == 1) {
        g_switch_membership_ok++;
    }
#endif

    hrt_set_policy(HRT_SCHED_PRIORITY);
#ifdef HARDRT_TEST_HOOKS
    if (hrt__test_ready_occurrences(g_switch_peer_a_id) == 1 &&
        hrt__test_ready_occurrences(g_switch_peer_b_id) == 1) {
        g_switch_membership_ok++;
    }
#endif

    g_switch_controller_stage = 3;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_runtime_policy_switch_rebuilds_ready_queues(void) {
    hrt__test_reset_scheduler_state();
    g_switch_controller_stage = 0;
    g_switch_peer_a_runs = 0;
    g_switch_peer_b_runs = 0;
    g_switch_membership_ok = 0;

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 7
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (runtime policy switch)");

    static uint32_t sc[2048], sa[2048], sb[2048];
    hrt_task_attr_t controller_attr = {.priority = HRT_PRIO0, .timeslice = 7};
    hrt_task_attr_t peer_a_attr = {.priority = HRT_PRIO1, .timeslice = 7};
    hrt_task_attr_t peer_b_attr = {.priority = HRT_PRIO3, .timeslice = 7};

    int controller = hrt_create_task(policy_switch_controller, NULL, sc, 2048, &controller_attr);
    g_switch_peer_b_id = hrt_create_task(policy_switch_peer_b, NULL, sb, 2048, &peer_b_attr);
    g_switch_peer_a_id = hrt_create_task(policy_switch_peer_a, NULL, sa, 2048, &peer_a_attr);
    T_ASSERT_TRUE(controller >= 0 && g_switch_peer_a_id >= 0 && g_switch_peer_b_id >= 0,
                  "created policy-switch tasks");

    hrt_start();

    T_ASSERT_EQ_INT(3, g_switch_controller_stage,
                    "runtime switch traversed PRIORITY -> RR -> PRIORITY_RR -> PRIORITY");
    T_ASSERT_EQ_INT(1, g_switch_peer_a_runs,
                    "peer A ran once while global RR was active");
    T_ASSERT_EQ_INT(1, g_switch_peer_b_runs,
                    "peer B ran once while global RR was active");
#ifdef HARDRT_TEST_HOOKS
    T_ASSERT_EQ_INT(3, g_switch_membership_ok,
                    "policy switches preserve exactly one READY membership per peer");
#endif
}

static const test_case_t CASES[] = {
    {"PRIORITY_RR rotation with yield (same priority)",
     test_priority_rr_rotation_with_yield_same_priority},
    {"Global RR ignores task priority",
     test_global_rr_ignores_priority},
    {"Global RR wake keeps current quantum",
     test_global_rr_wake_does_not_steal_quantum},
    {"Runtime policy switching rebuilds READY queues",
     test_runtime_policy_switch_rebuilds_ready_queues},
};

const test_case_t *get_tests_rr_yield(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
