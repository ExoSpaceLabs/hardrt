#include "test_common.h"
#include "hardrt_kernel.h"

#define STACK_WORDS 1024

static uint32_t g_zero_stack[STACK_WORDS];
static uint32_t g_peer_stack[STACK_WORDS];
static volatile int g_zero_id = -1;
static volatile int g_peer_runs = 0;
static volatile int g_peer_seen_state = -1;
static volatile int g_peer_ready_occurrences = -1;
static volatile uint32_t g_zero_elapsed = UINT32_MAX;

static void zero_sleep_task(void *arg) {
    (void)arg;
    const uint32_t start = hrt_tick_now();
    hrt_sleep(0);
    g_zero_elapsed = hrt_tick_now() - start;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void zero_sleep_peer(void *arg) {
    (void)arg;
    g_peer_runs++;
#ifdef HARDRT_TEST_HOOKS
    g_peer_seen_state = hrt__test_task_state(g_zero_id);
    g_peer_ready_occurrences = hrt__test_ready_occurrences(g_zero_id);
#endif
    hrt_task_delete();
}

static void run_zero_sleep_policy(const hrt_policy_t policy, const char *label) {
    hrt__test_reset_scheduler_state();
    g_zero_id = -1;
    g_peer_runs = 0;
    g_peer_seen_state = -1;
    g_peer_ready_occurrences = -1;
    g_zero_elapsed = UINT32_MAX;

    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = policy;
    cfg.default_slice = 3;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), label);

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 3 };
    g_zero_id = hrt_create_task(zero_sleep_task, NULL,
                                g_zero_stack, STACK_WORDS, &attr);
    const int peer_id = hrt_create_task(zero_sleep_peer, NULL,
                                        g_peer_stack, STACK_WORDS, &attr);
    T_ASSERT_TRUE(g_zero_id >= 0 && peer_id >= 0,
                  "sleep-zero fixture tasks created");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_peer_runs,
                    "hrt_sleep(0) creates a scheduling point for an eligible peer");
    T_ASSERT_EQ_UINT(0u, g_zero_elapsed,
                     "hrt_sleep(0) does not wait for or advance a tick");
#ifdef HARDRT_TEST_HOOKS
    T_ASSERT_EQ_INT(HRT_READY, g_peer_seen_state,
                    "sleep-zero caller is READY while yielded, never SLEEP");
    T_ASSERT_EQ_INT(1, g_peer_ready_occurrences,
                    "sleep-zero caller has exactly one READY membership");
#endif
}

static void test_sleep_zero_yields_under_all_policies(void) {
    run_zero_sleep_policy(HRT_SCHED_PRIORITY,
                          "init sleep-zero PRIORITY policy");
    run_zero_sleep_policy(HRT_SCHED_RR,
                          "init sleep-zero global RR policy");
    run_zero_sleep_policy(HRT_SCHED_PRIORITY_RR,
                          "init sleep-zero PRIORITY_RR policy");
}

static volatile uint32_t g_positive_elapsed = UINT32_MAX;

static void positive_subtick_sleeper(void *arg) {
    (void)arg;
    const uint32_t start = hrt_tick_now();
    hrt_sleep(1); /* 1 ms at 100 Hz is 0.1 tick and must round up to one tick. */
    g_positive_elapsed = hrt_tick_now() - start;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void one_tick_driver(void *arg) {
    (void)arg;
    hrt_tick_from_isr();
    for (;;) hrt_yield();
}

static void test_positive_subtick_sleep_rounds_up(void) {
    hrt__test_reset_scheduler_state();
    g_positive_elapsed = UINT32_MAX;

    hrt_config_t cfg = {0};
    cfg.tick_hz = 100;
    cfg.policy = HRT_SCHED_PRIORITY_RR;
    cfg.default_slice = 3;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    T_ASSERT_EQ_INT(0, hrt_init(&cfg),
                    "init positive sub-tick sleep test");

    const hrt_task_attr_t sleeper_attr = { .priority = HRT_PRIO0, .timeslice = 3 };
    const hrt_task_attr_t driver_attr = { .priority = HRT_PRIO1, .timeslice = 3 };
    T_ASSERT_TRUE(hrt_create_task(positive_subtick_sleeper, NULL,
                                  g_zero_stack, STACK_WORDS,
                                  &sleeper_attr) >= 0,
                  "created positive sub-tick sleeper");
    T_ASSERT_TRUE(hrt_create_task(one_tick_driver, NULL,
                                  g_peer_stack, STACK_WORDS,
                                  &driver_attr) >= 0,
                  "created one-tick driver");

    hrt_start();

    T_ASSERT_EQ_UINT(1u, g_positive_elapsed,
                     "positive sub-tick sleep still rounds up to one tick");
}

static const test_case_t CASES[] = {
    {"Sleep: hrt_sleep(0) yields immediately under all policies",
     test_sleep_zero_yields_under_all_policies},
    {"Sleep: positive sub-tick duration rounds up to one tick",
     test_positive_subtick_sleep_rounds_up},
};

const test_case_t *get_tests_sleep_zero(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
