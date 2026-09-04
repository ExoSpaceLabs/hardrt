#include "test_common.h"

#define WORKERS 3
#define STACK_WORDS 1024

static uint32_t g_worker_stacks[WORKERS][STACK_WORDS];
static uint32_t g_driver_stack[STACK_WORDS];
static volatile uint32_t g_delays[WORKERS];
static volatile int g_wake_order[64];
static volatile int g_wake_count;
static volatile int g_repeat_target;
static volatile int g_repeat_count;
static volatile int g_driver_limit;
static volatile int g_driver_ticks;

#ifdef HARDRT_TEST_HOOKS
void hrt__test_reset_wake_preempt_decisions(void);
uint32_t hrt__test_wake_preempt_decisions(void);
#endif

static void reset_fixture(void) {
    memset((void *)g_delays, 0, sizeof(g_delays));
    memset((void *)g_wake_order, -1, sizeof(g_wake_order));
    g_wake_count = 0;
    g_repeat_target = 0;
    g_repeat_count = 0;
    g_driver_limit = 100;
    g_driver_ticks = 0;
#ifdef HARDRT_TEST_HOOKS
    hrt__test_reset_wake_preempt_decisions();
#endif
}

static hrt_config_t external_cfg(void) {
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = HRT_SCHED_PRIORITY;
    cfg.default_slice = 0;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    return cfg;
}

static void ordered_sleeper(void *arg) {
    const int index = (int)(uintptr_t)arg;
    hrt_sleep(g_delays[index]);
    g_wake_order[g_wake_count++] = index;
    hrt_task_delete();
}

static void repeating_sleeper(void *arg) {
    (void)arg;
    for (int i = 0; i < g_repeat_target; ++i) {
        hrt_sleep(1);
        g_repeat_count++;
    }
    hrt_task_delete();
}

static void tick_driver(void *arg) {
    const int expected = (int)(uintptr_t)arg;

    for (int i = 0; i < g_driver_limit; ++i) {
        hrt_tick_from_isr();
        g_driver_ticks++;
        hrt_yield();

        if ((g_repeat_target > 0 && g_repeat_count >= expected) ||
            (g_repeat_target == 0 && g_wake_count >= expected)) {
            hrt__test_stop_scheduler();
            hrt_yield();
            return;
        }
    }

    hrt__test_stop_scheduler();
    hrt_yield();
}

static void create_ordered_workers(int count) {
    const hrt_task_attr_t worker_attr = { .priority = HRT_PRIO0, .timeslice = 0 };
    for (int i = 0; i < count; ++i) {
        T_ASSERT_TRUE(hrt_create_task(ordered_sleeper, (void *)(uintptr_t)i,
                                      g_worker_stacks[i], STACK_WORDS,
                                      &worker_attr) >= 0,
                      "created ordered sleeper");
    }
}

static void create_driver(int expected) {
    const hrt_task_attr_t driver_attr = { .priority = HRT_PRIO1, .timeslice = 0 };
    T_ASSERT_TRUE(hrt_create_task(tick_driver, (void *)(uintptr_t)expected,
                                  g_driver_stack, STACK_WORDS,
                                  &driver_attr) >= 0,
                  "created external tick driver");
}

static void test_equal_deadline_sleepers_keep_fifo_order(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init equal-deadline sleeper test");

    g_delays[0] = 5;
    g_delays[1] = 5;
    g_delays[2] = 5;
    create_ordered_workers(WORKERS);
    create_driver(WORKERS);

    hrt_start();

    T_ASSERT_EQ_INT(WORKERS, g_wake_count, "all equal-deadline sleepers woke");
    T_ASSERT_EQ_INT(0, g_wake_order[0], "equal deadline FIFO wake 0");
    T_ASSERT_EQ_INT(1, g_wake_order[1], "equal deadline FIFO wake 1");
    T_ASSERT_EQ_INT(2, g_wake_order[2], "equal deadline FIFO wake 2");
    T_ASSERT_EQ_INT(5, g_driver_ticks, "equal-deadline wake occurred on tick 5");
#ifdef HARDRT_TEST_HOOKS
    T_ASSERT_EQ_UINT(1u, hrt__test_wake_preempt_decisions(),
                     "simultaneous priority wake freezes preemption after first true decision");
#endif
}

static void test_equal_deadline_sleepers_keep_global_rr_fifo(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    hrt_config_t cfg = external_cfg();
    cfg.policy = HRT_SCHED_RR;
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init global-RR equal-deadline sleeper test");

    g_delays[0] = 5;
    g_delays[1] = 5;
    g_delays[2] = 5;
    create_ordered_workers(WORKERS);
    create_driver(WORKERS);

    hrt_start();

    T_ASSERT_EQ_INT(WORKERS, g_wake_count, "all global-RR equal-deadline sleepers woke");
    T_ASSERT_EQ_INT(0, g_wake_order[0], "global RR preserves equal-deadline FIFO wake 0");
    T_ASSERT_EQ_INT(1, g_wake_order[1], "global RR preserves equal-deadline FIFO wake 1");
    T_ASSERT_EQ_INT(2, g_wake_order[2], "global RR preserves equal-deadline FIFO wake 2");
    T_ASSERT_EQ_INT(5, g_driver_ticks, "global-RR equal-deadline wake occurred on tick 5");
}

static void test_staggered_sleepers_wake_by_deadline(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init staggered sleeper test");

    g_delays[0] = 3;
    g_delays[1] = 1;
    g_delays[2] = 2;
    create_ordered_workers(WORKERS);
    create_driver(WORKERS);

    hrt_start();

    T_ASSERT_EQ_INT(WORKERS, g_wake_count, "all staggered sleepers woke");
    T_ASSERT_EQ_INT(1, g_wake_order[0], "1-tick sleeper woke first");
    T_ASSERT_EQ_INT(2, g_wake_order[1], "2-tick sleeper woke second");
    T_ASSERT_EQ_INT(0, g_wake_order[2], "3-tick sleeper woke third");
    T_ASSERT_EQ_INT(3, g_driver_ticks, "staggered set completed on tick 3");
}

static void test_repeated_sleep_wake_cycles(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init repeated sleeper test");

    g_repeat_target = 20;
    g_driver_limit = 40;
    const hrt_task_attr_t worker_attr = { .priority = HRT_PRIO0, .timeslice = 0 };
    T_ASSERT_TRUE(hrt_create_task(repeating_sleeper, NULL,
                                  g_worker_stacks[0], STACK_WORDS,
                                  &worker_attr) >= 0,
                  "created repeating sleeper");
    create_driver(g_repeat_target);

    hrt_start();

    T_ASSERT_EQ_INT(g_repeat_target, g_repeat_count,
                    "repeating sleeper completed every sleep/wake cycle");
    T_ASSERT_EQ_INT(g_repeat_target, g_driver_ticks,
                    "one-tick repeated sleeper consumed exactly one tick per cycle");
}

static void test_sleep_order_survives_tick_wrap(void) {
#ifdef HARDRT_TEST_HOOKS
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init wrap-order sleeper test");
    hrt__test_set_tick(0xFFFFFFFCu);

    g_delays[0] = 2;
    g_delays[1] = 5;
    g_delays[2] = 3;
    create_ordered_workers(WORKERS);
    create_driver(WORKERS);

    hrt_start();

    T_ASSERT_EQ_INT(WORKERS, g_wake_count, "all wrap-order sleepers woke");
    T_ASSERT_EQ_INT(0, g_wake_order[0], "2-tick sleeper woke first across wrap");
    T_ASSERT_EQ_INT(2, g_wake_order[1], "3-tick sleeper woke second across wrap");
    T_ASSERT_EQ_INT(1, g_wake_order[2], "5-tick sleeper woke third across wrap");
    T_ASSERT_EQ_INT(5, g_driver_ticks, "wrap-order set completed after five ticks");
    T_ASSERT_EQ_UINT(1u, hrt__test_get_tick(), "tick counter wrapped to 1");
#else
    printf("SKIP: wrap-order test requires HARDRT_TEST_HOOKS.\n");
#endif
}

static const test_case_t CASES[] = {
    {"Sleep queue: equal deadlines preserve FIFO order", test_equal_deadline_sleepers_keep_fifo_order},
    {"Sleep queue: equal deadlines preserve global RR FIFO", test_equal_deadline_sleepers_keep_global_rr_fifo},
    {"Sleep queue: staggered deadlines wake in order", test_staggered_sleepers_wake_by_deadline},
    {"Sleep queue: repeated sleep/wake cycles", test_repeated_sleep_wake_cycles},
    {"Sleep queue: ordering survives 32-bit tick wrap", test_sleep_order_survives_tick_wrap},
};

const test_case_t *get_tests_sleep_queue(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
