/* Legacy monolithic test suite used during early development; kept for reference.
 * Current tests are split into focused groups under tests/test_*.c and executed
 * by test_main.c. */
#include "heartos.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* Test hook provided by POSIX port when built with HEARTOS_TEST_HOOKS */
#ifdef __cplusplus
extern "C" {

#endif
void hrt__test_stop_scheduler(void);

void hrt__test_reset_scheduler_state(void);
#ifdef __cplusplus
}
#endif

/* Tiny assert helpers with colored PASS/FAIL output */
static int g_failures = 0;
#define ANSI_GRN "\x1b[32m"
#define ANSI_RED "\x1b[31m"
#define ANSI_RST "\x1b[0m"

#define T_ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        ++g_failures; \
        printf(ANSI_RED "FAIL" ANSI_RST ": %s\n", msg); \
        printf("  ASSERT: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    } else { \
        printf(ANSI_GRN "PASS" ANSI_RST ": %s\n", msg); \
    } \
} while(0)

#define T_ASSERT_EQ_INT(exp, got, msg) do { \
    if ((exp) != (got)) { \
        ++g_failures; \
        printf(ANSI_RED "FAIL" ANSI_RST ": %s\n", msg); \
        printf("  expected %d, got %d (%s:%d)\n", (int)(exp), (int)(got), __FILE__, __LINE__); \
    } else { \
        printf(ANSI_GRN "PASS" ANSI_RST ": %s (=%d)\n", msg, (int)(got)); \
    } \
} while(0)

#define T_ASSERT_STREQ(exp, got, msg) do { \
    if (strcmp((exp),(got)) != 0) { \
        ++g_failures; \
        printf(ANSI_RED "FAIL" ANSI_RST ": %s\n", msg); \
        printf("  expected '%s', got '%s' (%s:%d)\n", (exp), (got), __FILE__, __LINE__); \
    } else { \
        printf(ANSI_GRN "PASS" ANSI_RST ": %s ('%s')\n", msg, (got)); \
    } \
} while(0)

/* ---------------- Test 1: version and port identity ---------------- */
static void test_version_and_port_identity(void) {
    const char *ver = hrt_version_string();
    unsigned v32 = hrt_version_u32();
    T_ASSERT_TRUE(ver && strlen(ver) > 0, "version string not empty");
    /* Very loose check: 0.2.0 encodes to 0x000200, but don't hard-code; just non-zero */
    T_ASSERT_TRUE(v32 != 0, "version u32 non-zero");

    const char *port = hrt_port_name();
    int pid = hrt_port_id();
    T_ASSERT_STREQ("posix", port, "port name should be posix under POSIX tests");
    T_ASSERT_EQ_INT(1, pid, "port id should be 1 for posix");
}

/* ---------------- Test 2: sleep/wake cycles and scheduler stop ---------------- */
static volatile int g_wake_count = 0;
static int g_target_wakes = 5;
static uint32_t g_start_tick = 0;

static void sleeper_task(void *arg) {
    (void) arg;
    for (;;) {
        hrt_sleep(10); /* 10 ms */
        ++g_wake_count;
        if (g_wake_count >= g_target_wakes) {
            /* Request scheduler stop and yield back to scheduler */
            hrt__test_stop_scheduler();
            hrt_yield();
        }
    }
}

static void test_sleep_wake_and_stop(void) {
    /* Config: 1 kHz tick, RR policy, default slice 5 */
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0");

    static uint32_t stack_sleeper[2048];
    hrt_task_attr_t attr = {.priority = HRT_PRIO1, .timeslice = 5};
    int tid = hrt_create_task(sleeper_task, NULL, stack_sleeper, sizeof(stack_sleeper) / sizeof(stack_sleeper[0]),
                              &attr);
    T_ASSERT_TRUE(tid >= 0, "task created");

    g_start_tick = hrt_tick_now();

    /* Enter scheduler; it will return after sleeper task requests stop. */
    hrt_start();

    /* Back from scheduler: verify wake count and that time advanced */
    T_ASSERT_EQ_INT(g_target_wakes, g_wake_count, "sleeper should have woken desired number of times");
    uint32_t elapsed = hrt_tick_now() - g_start_tick;
    T_ASSERT_TRUE(elapsed >= (uint32_t)(g_target_wakes * 10), "tick advanced at least expected amount");
}

/* ---------------- Test 3: RR rotation with yield (same priority) ---------------- */
static volatile int g_yield_a = 0, g_yield_b = 0;
static volatile int g_yield_total = 0;
static const int g_yield_target = 200;

static void rr_yield_task_a(void *arg) {
    (void) arg;
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
    (void) arg;
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

static void test_rr_rotation_with_yield_same_priority(void) {
    /* Two tasks at same priority with RR; yielding should alternate them fairly. */
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (RR yield test)");

    static uint32_t sa[2048], sb[2048];
    hrt_task_attr_t attr = {.priority = HRT_PRIO1, .timeslice = 3};
    int a = hrt_create_task(rr_yield_task_a, NULL, sa, sizeof(sa) / sizeof(sa[0]), &attr);
    int b = hrt_create_task(rr_yield_task_b, NULL, sb, sizeof(sb) / sizeof(sb[0]), &attr);
    T_ASSERT_TRUE(a >= 0 && b >= 0, "two RR tasks created (yield test)");

    g_yield_a = g_yield_b = g_yield_total = 0;
    hrt_start();

    /* After scheduler returns, both should have run many times and fairly close */
    T_ASSERT_TRUE(g_yield_a > 0 && g_yield_b > 0, "both RR-yield tasks made progress");
    int diff = g_yield_a - g_yield_b;
    if (diff < 0) diff = -diff;
    T_ASSERT_TRUE(diff <= g_yield_target/5, "RR with yield should distribute fairly (diff <= 20%)");
}

/* ---------------- Test 4: RR rotation with sleeping tasks (same priority) ---------------- */
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

/* ---------------- Test 5: Strict priority dominance (PRIORITY policy) ---------------- */
static volatile int g_prio_high_iters = 0;
static volatile int g_prio_low_iters = 0;
static volatile int g_prio_high_slept = 0; /* flag set by high when it sleeps */
static volatile int g_prio_low_before_sleep = 0; /* low-prio runs counted before high-ever-slept */

static void prio_high_task(void *arg) {
    (void) arg;
    for (;;) {
        ++g_prio_high_iters;
        /* Keep yielding to re-enter scheduler; with PRIORITY policy, low prio must not run */
        if (g_prio_high_iters >= 2000) {
            /* allow low priority to run once */
            g_prio_high_slept = 1;
            hrt_sleep(1);
            /* after wake, stop */
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_yield();
    }
}

static void prio_low_task(void *arg) {
    (void) arg;
    for (;;) {
        if (!g_prio_high_slept) {
            ++g_prio_low_before_sleep;
        }
        ++g_prio_low_iters;
        /* Once it runs, nap to avoid hogging and loop */
        hrt_sleep(1);
    }
}

static void test_priority_dominance_priority_policy(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY, .default_slice = 0};
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (PRIORITY policy)");

    static uint32_t sh[2048], sl[2048];
    hrt_task_attr_t ah = {.priority = HRT_PRIO0, .timeslice = 0}; /* cooperative high */
    hrt_task_attr_t al = {.priority = HRT_PRIO1, .timeslice = 0};
    int th = hrt_create_task(prio_high_task, NULL, sh, sizeof(sh) / sizeof(sh[0]), &ah);
    int tl = hrt_create_task(prio_low_task, NULL, sl, sizeof(sl) / sizeof(sl[0]), &al);
    T_ASSERT_TRUE(th >= 0 && tl >= 0, "created high and low priority tasks");

    g_prio_high_iters = g_prio_low_iters = 0;
    g_prio_high_slept = 0;
    g_prio_low_before_sleep = 0;
    hrt_start();

    T_ASSERT_EQ_INT(0, g_prio_low_before_sleep, "low prio should not run while high prio remained READY");
    T_ASSERT_TRUE(g_prio_high_iters >= 2000, "high prio performed expected iterations");
    /* After high prio slept once, low prio should have run at least once before stop */
    T_ASSERT_TRUE(g_prio_low_iters >= 1, "low prio should run after high prio sleeps");
}

/* ---------------- Test 6: Cooperative vs RR mix within same priority ---------------- */
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

/* ---------------- Test 7: Tick rate configurability (200 Hz) ---------------- */
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

typedef void (*test_fn_t)(void);

typedef struct {
    const char *name;
    test_fn_t fn;
} test_case_t;

int main(void) {
    /* Register test cases in desired order */
    const test_case_t cases[] = {
        {"Version and Port Identity", test_version_and_port_identity},
        {"Sleep/Wake and Stop", test_sleep_wake_and_stop},
        {"RR rotation with yield (same priority)", test_rr_rotation_with_yield_same_priority},
        {"RR rotation with sleep (same priority)", test_rr_rotation_with_sleep_same_priority},
        {"Strict priority dominance (PRIORITY policy)", test_priority_dominance_priority_policy},
        {"Cooperative vs RR mix (same priority)", test_cooperative_vs_rr_same_priority},
        {"Tick rate configurability (200 Hz)", test_tick_rate_200hz_sleep_accuracy},
    };
    const int total = (int) (sizeof(cases) / sizeof(cases[0]));

    int tests_failed = 0;
    int tests_passed = 0;

    for (int i = 0; i < total; ++i) {
        int before = g_failures; /* snapshot assertion failures */
        printf("\n==== Test %d/%d: %s ====%s\n", i + 1, total, cases[i].name, "");
        cases[i].fn();
        int after = g_failures;
        int case_failures = after - before;
        if (case_failures == 0) {
            ++tests_passed;
            printf(ANSI_GRN "RESULT" ANSI_RST ": Test %d PASSED (%s)\n", i + 1, cases[i].name);
        } else {
            ++tests_failed;
            printf(ANSI_RED "RESULT" ANSI_RST ": Test %d FAILED (%s) — %d assertion failure(s)\n",
                   i + 1, cases[i].name, case_failures);
        }
    }

    printf("\n================ Summary ================\n");
    printf("Total tests: %d\n", total);
    printf(ANSI_GRN "  Passed: %d" ANSI_RST "\n", tests_passed);
    if (tests_failed > 0) {
        printf(ANSI_RED "  Failed: %d" ANSI_RST "\n", tests_failed);
    } else {
        printf("  Failed: %d\n", tests_failed);
    }
    printf("========================================\n\n");

    if (g_failures == 0) {
        printf("All tests passed.\n");
        return 0;
    } else {
        printf("%d assertion failure(s) in %d test(s).\n", g_failures, tests_failed);
        return 1;
    }
}
