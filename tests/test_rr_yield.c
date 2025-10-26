#include "test_common.h"

static volatile int g_yield_a = 0, g_yield_b = 0;
static volatile int g_yield_total = 0;
static const int g_yield_target = 200;

static void rr_yield_task_a(void* arg){ (void)arg; for(;;){
        ++g_yield_a; ++g_yield_total; if (g_yield_total >= g_yield_target){ hrt__test_stop_scheduler(); hrt_yield(); }
        hrt_yield();
    } }
static void rr_yield_task_b(void* arg){ (void)arg; for(;;){
        ++g_yield_b; ++g_yield_total; if (g_yield_total >= g_yield_target){ hrt__test_stop_scheduler(); hrt_yield(); }
        hrt_yield();
    } }

static void test_rr_rotation_with_yield_same_priority(void){
    /* Two tasks at same priority with RR; yielding should alternate them fairly. */
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = { .tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3 };
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (RR yield test)");

    static uint32_t sa[2048], sb[2048];
    hrt_task_attr_t attr = { .priority = HRT_PRIO1, .timeslice = 3 };
    int a = hrt_create_task(rr_yield_task_a, NULL, sa, sizeof(sa)/sizeof(sa[0]), &attr);
    int b = hrt_create_task(rr_yield_task_b, NULL, sb, sizeof(sb)/sizeof(sb[0]), &attr);
    T_ASSERT_TRUE(a >= 0 && b >= 0, "two RR tasks created (yield test)");

    g_yield_a = g_yield_b = g_yield_total = 0;
    hrt_start();

    /* After scheduler returns, both should have run many times and fairly close */
    T_ASSERT_TRUE(g_yield_a > 0 && g_yield_b > 0, "both RR-yield tasks made progress");
    int diff = g_yield_a - g_yield_b; if (diff < 0) diff = -diff;
    T_ASSERT_TRUE(diff <= g_yield_target/5, "RR with yield should distribute fairly (diff <= 20%)");
}

static const test_case_t CASES[] = {
    {"RR rotation with yield (same priority)", test_rr_rotation_with_yield_same_priority},
};

const test_case_t* get_tests_rr_yield(int* out_count){
    if (out_count) *out_count = (int)(sizeof(CASES)/sizeof(CASES[0]));
    return CASES;
}
