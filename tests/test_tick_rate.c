/* Validates tick-rate configurability, deferred activation, and ms->tick accuracy. */
#define _POSIX_C_SOURCE 200809L
#include "test_common.h"

#include <signal.h>
#include <time.h>

extern volatile hrt_err g_error;

static volatile int g_200hz_wakes = 0;
static int g_200hz_target = 10;
static uint32_t g_200hz_start = 0;

static void sleeper_200hz(void *arg) {
    (void) arg;
    for (;;) {
        hrt_sleep(10); /* 10 ms */
        ++g_200hz_wakes;
        if (g_200hz_wakes >= g_200hz_target) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
    }
}

static void test_internal_tick_dormant_until_start(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should configure internal tick");

    const uint32_t before = hrt_tick_now();
    const struct timespec wait = {0, 20 * 1000 * 1000};
    nanosleep(&wait, NULL);
    const uint32_t after = hrt_tick_now();

    T_ASSERT_EQ_UINT(before, after,
                     "internal periodic tick remains inactive before hrt_start");
}

static void test_unrepresentable_host_tick_rejected(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = HRT_TICK_HZ_MAX,
                        .policy = HRT_SCHED_PRIORITY_RR,
                        .default_slice = 5,
                        .tick_src = HRT_TICK_SYSTICK};
    T_ASSERT_TRUE(hrt_init(&cfg) < 0,
                  "POSIX rejects an internal tick period below nanosecond resolution");
}

static void test_internal_mode_diagnoses_external_tick_api(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000,
                        .policy = HRT_SCHED_PRIORITY_RR,
                        .default_slice = 5,
                        .tick_src = HRT_TICK_SYSTICK};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should configure internal tick for mismatch test");

    g_error = NONE;
    const uint32_t before = hrt_tick_now();
    hrt_tick_from_isr();

    T_ASSERT_EQ_UINT(before, hrt_tick_now(),
                     "external tick API must not advance time in internal mode");
    T_ASSERT_EQ_INT(ERR_TICK_SOURCE_MISMATCH, g_error,
                    "external tick API misuse records tick-source mismatch");
}

static void test_reset_drains_pending_preempt_signal(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000,
                        .policy = HRT_SCHED_PRIORITY_RR,
                        .default_slice = 5,
                        .tick_src = HRT_TICK_SYSTICK};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init pending-preemption reset fixture");

    hrt__test_block_sigalrm();
    T_ASSERT_EQ_INT(0, raise(SIGALRM), "queue a blocked POSIX preemption signal before reset");

    sigset_t pending;
    T_ASSERT_EQ_INT(0, sigpending(&pending), "query pending signals before reset");
    T_ASSERT_EQ_INT(1, sigismember(&pending, SIGALRM),
                    "POSIX preemption signal is pending while blocked");

    hrt__test_reset_scheduler_state();

    T_ASSERT_EQ_INT(0, sigpending(&pending), "query pending signals after reset");
    T_ASSERT_EQ_INT(0, sigismember(&pending, SIGALRM),
                    "reset consumes stale pending POSIX preemption signal");
    hrt__test_unblock_sigalrm();
}

static void test_tick_rate_200hz_sleep_accuracy(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 200, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (200 Hz)");

    static uint32_t st[2048];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    int tid = hrt_create_task(sleeper_200hz, NULL, st, sizeof(st) / sizeof(st[0]), &a);
    T_ASSERT_TRUE(tid >= 0, "created 200 Hz sleeper task");

    g_200hz_wakes = 0;
    g_200hz_start = hrt_tick_now();
    hrt_start();

    uint32_t elapsed_ticks = hrt_tick_now() - g_200hz_start;
    int expected = g_200hz_target * (10 * 200) / 1000; /* wakes * sleep_ms * hz / 1000 */
    T_ASSERT_TRUE((int)elapsed_ticks >= expected, "elapsed ticks should be >= expected at 200 Hz");
    T_ASSERT_TRUE((int)elapsed_ticks <= expected + g_200hz_target + 2,
                  "elapsed ticks should not exceed expected by large margin");
}

static const test_case_t CASES[] = {
    {"Internal tick remains dormant until scheduler start", test_internal_tick_dormant_until_start},
    {"Internal tick rejects unrepresentable host period", test_unrepresentable_host_tick_rejected},
    {"Internal mode diagnoses external tick API misuse", test_internal_mode_diagnoses_external_tick_api},
    {"POSIX reset drains pending preemption signal", test_reset_drains_pending_preempt_signal},
    {"Tick rate configurability (200 Hz)", test_tick_rate_200hz_sleep_accuracy},
};

const test_case_t *get_tests_tick_rate(int *out_count) {
    if (out_count) *out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
