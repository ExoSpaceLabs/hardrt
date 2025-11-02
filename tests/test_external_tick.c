/* Tests for external tick usage: valid and invalid scenarios */
#include "test_common.h"
#include "heartos_time.h"

#ifdef HEARTOS_TEST_HOOKS
void hrt__test_block_sigalrm(void);
void hrt__test_unblock_sigalrm(void);
#endif

static void test_tick_from_isr_before_init_safe_noop(void) {
    /* Calling before init should not crash and should not advance tick */
    uint32_t before = hrt_tick_now();
    hrt_tick_from_isr();
    uint32_t after = hrt_tick_now();
    T_ASSERT_EQ_INT((int)before, (int)after, "tick_from_isr before init is a no-op");
}

static void test_external_tick_advances_only_on_manual_calls(void) {
    /* Configure EXTERNAL; no auto SIGALRM tick should run on POSIX */
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = HRT_SCHED_PRIORITY_RR;
    cfg.default_slice = 5;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init external tick");

    uint32_t start = hrt_tick_now();
    /* Wait a tiny bit of wall time; tick must not change without manual calls */
    for (volatile int i = 0; i < 1000000; ++i) { /* quick spin */ }
    uint32_t noauto = hrt_tick_now();
    T_ASSERT_EQ_INT((int)start, (int)noauto, "no auto tick in EXTERNAL mode");

    /* Now advance manually N times */
    const int N = 25;
    for (int i = 0; i < N; ++i) hrt_tick_from_isr();
    uint32_t after = hrt_tick_now();
    T_ASSERT_EQ_INT(N, (int)(after - start), "manual hrt_tick_from_isr advances tick by N");
}

static volatile int g_sleeper_woke = 0;
static void sleeper_task(void *arg) {
    (void)arg;
    hrt_sleep(5); /* 5 ms/ticks */
    g_sleeper_woke = 1;
    /* Let the tick driver notice and stop the scheduler */
    hrt_yield();
}

static void tick_driver_task(void *arg) {
    (void)arg;
    for (;;) {
        hrt_tick_from_isr();
        if (g_sleeper_woke) {
            /* Request scheduler stop once the sleeper has woken */
            hrt__test_stop_scheduler();
            hrt_yield();
        } else {
            hrt_yield();
        }
    }
}

static void test_external_tick_wakes_sleepers_on_manual_ticks(void) {
    hrt__test_reset_scheduler_state();
    g_sleeper_woke = 0;

    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = HRT_SCHED_PRIORITY_RR;
    cfg.default_slice = 3;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init external tick for wake test");

    static uint32_t st_sleep[1024];
    static uint32_t st_tick[1024];
    hrt_task_attr_t a1 = { .priority = HRT_PRIO1, .timeslice = 2 };
    hrt_task_attr_t a2 = { .priority = HRT_PRIO1, .timeslice = 1 };
    int tid1 = hrt_create_task(sleeper_task, NULL, st_sleep, (int)(sizeof(st_sleep)/sizeof(st_sleep[0])), &a1);
    T_ASSERT_TRUE(tid1 >= 0, "created sleeper task");
    int tid2 = hrt_create_task(tick_driver_task, NULL, st_tick, (int)(sizeof(st_tick)/sizeof(st_tick[0])), &a2);
    T_ASSERT_TRUE(tid2 >= 0, "created tick driver task");

    /* Start scheduler; tick driver will advance ticks and stop when sleeper wakes */
    hrt_start();

    T_ASSERT_EQ_INT(1, g_sleeper_woke, "sleeper woke after manual ticks");
}

static void test_systick_mode_ignores_manual_tick_from_isr(void) {
    /* In SYSTICK mode, POSIX port runs SIGALRM; to assert manual calls are ignored,
       block SIGALRM around the calls and check the tick doesn't advance from manual calls. */

    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = HRT_SCHED_PRIORITY_RR;
    cfg.default_slice = 5;
    cfg.tick_src = HRT_TICK_SYSTICK;
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init systick mode");

#ifdef HEARTOS_TEST_HOOKS
    hrt__test_block_sigalrm();
#endif
    uint32_t before = hrt_tick_now();
    for (int i = 0; i < 10; ++i) hrt_tick_from_isr();
    uint32_t after = hrt_tick_now();
#ifdef HEARTOS_TEST_HOOKS
    hrt__test_unblock_sigalrm();
#endif
    T_ASSERT_EQ_INT((int)before, (int)after, "manual tick_from_isr ignored in SYSTICK mode");
}

static const test_case_t CASES[] = {
    {"tick_from_isr before init is safe no-op", test_tick_from_isr_before_init_safe_noop},
    {"External tick: no auto-tick; manual calls advance", test_external_tick_advances_only_on_manual_calls},
    {"External tick: sleepers wake after manual ticks", test_external_tick_wakes_sleepers_on_manual_ticks},
    {"SYSTICK mode: manual tick_from_isr has no effect", test_systick_mode_ignores_manual_tick_from_isr},
};

const test_case_t *get_tests_external_tick(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES)/sizeof(CASES[0]));
    return CASES;
}
