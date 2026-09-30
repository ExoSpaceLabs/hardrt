#include "test_common.h"

#define STACK_WORDS 1024

static uint32_t g_worker_stack[STACK_WORDS];
static uint32_t g_driver_stack[STACK_WORDS];
static volatile hrt_delay_result_t g_result;
static volatile hrt_tick_t g_lateness;
static volatile hrt_tick_t g_observed_tick;
static volatile hrt_tick_t g_deadline;
static volatile hrt_tick_t g_periodic_ticks[3];
static volatile hrt_delay_result_t g_periodic_results[3];
static volatile hrt_tick_t g_periodic_lateness[3];

static hrt_config_t external_cfg(hrt_policy_t policy) {
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000u;
    cfg.policy = policy;
    cfg.default_slice = 0u;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    return cfg;
}

static void reset_fixture(void) {
    g_result = HRT_DELAY_INVALID_CONTEXT;
    g_lateness = UINT32_MAX;
    g_observed_tick = UINT32_MAX;
    g_deadline = 0u;
    for (int i = 0; i < 3; ++i) {
        g_periodic_ticks[i] = UINT32_MAX;
        g_periodic_results[i] = HRT_DELAY_INVALID_CONTEXT;
        g_periodic_lateness[i] = UINT32_MAX;
    }
}

static void stop_from_task(void) {
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void one_shot_waiter(void *arg) {
    (void)arg;
    hrt_tick_t late = UINT32_MAX;
    g_result = hrt_delay_until(g_deadline, &late);
    g_lateness = late;
    g_observed_tick = hrt_tick_now();
    stop_from_task();
}

static void yielding_tick_driver(void *arg) {
    const int limit = (int)(uintptr_t)arg;
    for (int i = 0; i < limit; ++i) {
        hrt_tick_from_isr();
        hrt_yield();
    }
    stop_from_task();
}

static void rr_late_tick_driver(void *arg) {
    (void)arg;
    /* Global RR + zero timeslice means waking the waiter does not preempt this
       running task. Advance one tick past the deadline before yielding. */
    for (int i = 0; i < 6; ++i) hrt_tick_from_isr();
    hrt_yield();
    stop_from_task();
}

static void periodic_waiter(void *arg) {
    (void)arg;
    hrt_tick_t next_release = hrt_tick_now();

    for (int i = 0; i < 3; ++i) {
        next_release += 5u;

        /* Consume two ticks of this period before the absolute wait. This
           stands in for elapsed application/interference time and proves the
           remaining delay is derived from the stable phase, not from now. */
        hrt_sleep(2u);

        hrt_tick_t late = UINT32_MAX;
        g_periodic_results[i] = hrt_delay_until(next_release, &late);
        g_periodic_lateness[i] = late;
        g_periodic_ticks[i] = hrt_tick_now();
    }

    stop_from_task();
}

static void test_delay_until_equal_deadline_is_on_time(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init exact-deadline test");
    hrt__test_set_tick(42u);
    g_deadline = 42u;

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(one_shot_waiter, NULL, g_worker_stack,
                                  STACK_WORDS, &attr) >= 0,
                  "created exact-deadline waiter");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_DELAY_OK, g_result, "exact deadline is on time");
    T_ASSERT_EQ_UINT(0u, g_lateness, "exact deadline has zero lateness");
    T_ASSERT_EQ_UINT(42u, g_observed_tick, "exact deadline does not consume a tick");
}

static void test_delay_until_reports_late_entry(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init late-entry test");
    hrt__test_set_tick(100u);
    g_deadline = 97u;

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(one_shot_waiter, NULL, g_worker_stack,
                                  STACK_WORDS, &attr) >= 0,
                  "created late-entry waiter");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_DELAY_MISSED, g_result, "late caller sees deadline miss");
    T_ASSERT_EQ_UINT(3u, g_lateness, "late caller reports ticks late");
    T_ASSERT_EQ_UINT(100u, g_observed_tick, "late call does not add another period");
}

static void test_delay_until_future_deadline_wakes_on_time(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init future-deadline test");
    g_deadline = 5u;

    const hrt_task_attr_t waiter_attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver_attr = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(one_shot_waiter, NULL, g_worker_stack,
                                  STACK_WORDS, &waiter_attr) >= 0,
                  "created future waiter");
    T_ASSERT_TRUE(hrt_create_task(yielding_tick_driver, (void *)(uintptr_t)8u,
                                  g_driver_stack, STACK_WORDS, &driver_attr) >= 0,
                  "created future tick driver");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_DELAY_OK, g_result, "future deadline returns on time");
    T_ASSERT_EQ_UINT(0u, g_lateness, "on-time future deadline has zero lateness");
    T_ASSERT_EQ_UINT(5u, g_observed_tick, "waiter resumes at nominal deadline");
}

static void test_delay_until_detects_dispatch_lateness(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init delayed-dispatch test");
    g_deadline = 5u;

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(one_shot_waiter, NULL, g_worker_stack,
                                  STACK_WORDS, &attr) >= 0,
                  "created RR deadline waiter");
    T_ASSERT_TRUE(hrt_create_task(rr_late_tick_driver, NULL, g_driver_stack,
                                  STACK_WORDS, &attr) >= 0,
                  "created RR late tick driver");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_DELAY_MISSED, g_result,
                    "on-time expiry but late dispatch is a miss");
    T_ASSERT_EQ_UINT(1u, g_lateness, "dispatch lateness is reported");
    T_ASSERT_EQ_UINT(6u, g_observed_tick, "waiter observes actual continuation tick");
}

static void test_delay_until_preserves_periodic_phase(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init stable-phase test");

    const hrt_task_attr_t waiter_attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver_attr = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(periodic_waiter, NULL, g_worker_stack,
                                  STACK_WORDS, &waiter_attr) >= 0,
                  "created periodic waiter");
    T_ASSERT_TRUE(hrt_create_task(yielding_tick_driver, (void *)(uintptr_t)20u,
                                  g_driver_stack, STACK_WORDS, &driver_attr) >= 0,
                  "created periodic tick driver");
    hrt_start();

    for (int i = 0; i < 3; ++i) {
        T_ASSERT_EQ_INT(HRT_DELAY_OK, g_periodic_results[i],
                        "periodic release is on time");
        T_ASSERT_EQ_UINT(0u, g_periodic_lateness[i],
                         "periodic release has zero lateness");
        T_ASSERT_EQ_UINT((unsigned)((i + 1) * 5), g_periodic_ticks[i],
                         "elapsed work consumes slack without phase drift");
    }
}

static void test_delay_until_wraps_cleanly(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init wrap deadline test");
    hrt__test_set_tick(0xFFFFFFFCu);
    g_deadline = 1u;

    const hrt_task_attr_t waiter_attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver_attr = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(one_shot_waiter, NULL, g_worker_stack,
                                  STACK_WORDS, &waiter_attr) >= 0,
                  "created wrap waiter");
    T_ASSERT_TRUE(hrt_create_task(yielding_tick_driver, (void *)(uintptr_t)8u,
                                  g_driver_stack, STACK_WORDS, &driver_attr) >= 0,
                  "created wrap tick driver");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_DELAY_OK, g_result, "wrapped future deadline returns on time");
    T_ASSERT_EQ_UINT(0u, g_lateness, "wrapped deadline has zero lateness");
    T_ASSERT_EQ_UINT(1u, g_observed_tick, "absolute deadline crosses uint32 wrap");
}

static void test_delay_until_rejects_half_range_ambiguity(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init half-range test");
    hrt__test_set_tick(0u);
    g_deadline = HRT_TICK_HALF_RANGE;

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(one_shot_waiter, NULL, g_worker_stack,
                                  STACK_WORDS, &attr) >= 0,
                  "created half-range waiter");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_DELAY_INVALID_DEADLINE, g_result,
                    "exact half-range deadline is rejected");
    T_ASSERT_EQ_UINT(0u, g_lateness, "invalid deadline does not invent lateness");
}

static void test_delay_until_rejects_non_task_context(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init invalid-context test");

    hrt_tick_t late = UINT32_MAX;
    const hrt_delay_result_t result = hrt_delay_until(5u, &late);
    T_ASSERT_EQ_INT(HRT_DELAY_INVALID_CONTEXT, result,
                    "delay_until requires a running application task");
    T_ASSERT_EQ_UINT(0u, late, "invalid context clears lateness output");
}

static const test_case_t CASES[] = {
    {"Delay-until: exact deadline is on time", test_delay_until_equal_deadline_is_on_time},
    {"Delay-until: late entry reports miss", test_delay_until_reports_late_entry},
    {"Delay-until: future deadline wakes on time", test_delay_until_future_deadline_wakes_on_time},
    {"Delay-until: delayed dispatch reports lateness", test_delay_until_detects_dispatch_lateness},
    {"Delay-until: periodic phase does not drift", test_delay_until_preserves_periodic_phase},
    {"Delay-until: absolute deadline survives tick wrap", test_delay_until_wraps_cleanly},
    {"Delay-until: half-range ambiguity is rejected", test_delay_until_rejects_half_range_ambiguity},
    {"Delay-until: non-task context is rejected", test_delay_until_rejects_non_task_context},
};

const test_case_t *get_tests_delay_until(int *out_count) {
    if (out_count != NULL) {
        *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    }
    return CASES;
}
