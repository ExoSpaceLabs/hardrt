/* Validates tick-rate configurability (e.g., 200 Hz) and ms→tick conversion accuracy. */
#include "test_common.h"

static volatile int g_200hz_wakes = 0;
static int g_200hz_target = 10;
static uint32_t g_200hz_start = 0;

static void sleeper_200hz(void* arg){ (void)arg; for(;;){
        hrt_sleep(10); /* 10 ms */
        ++g_200hz_wakes;
        if (g_200hz_wakes >= g_200hz_target){
            hrt__test_stop_scheduler();
            hrt_yield();
        }
    } }

static void test_tick_rate_200hz_sleep_accuracy(void){
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = { .tick_hz = 200, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5 };
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0 (200 Hz)");

    static uint32_t st[2048];
    hrt_task_attr_t a = { .priority = HRT_PRIO1, .timeslice = 3 };
    int tid = hrt_create_task(sleeper_200hz, NULL, st, sizeof(st)/sizeof(st[0]), &a);
    T_ASSERT_TRUE(tid >= 0, "created 200 Hz sleeper task");

    g_200hz_wakes = 0;
    g_200hz_start = hrt_tick_now();
    hrt_start();

    uint32_t elapsed_ticks = hrt_tick_now() - g_200hz_start;
    int expected = g_200hz_target * (10 * 200) / 1000; /* wakes * sleep_ms * hz / 1000 */
    T_ASSERT_TRUE((int)elapsed_ticks >= expected, "elapsed ticks should be >= expected at 200 Hz");
    T_ASSERT_TRUE((int)elapsed_ticks <= expected + g_200hz_target + 2, "elapsed ticks should not exceed expected by large margin");
}

static const test_case_t CASES[] = {
    {"Tick rate configurability (200 Hz)", test_tick_rate_200hz_sleep_accuracy},
};

const test_case_t* get_tests_tick_rate(int* out_count){
    if (out_count) *out_count = (int)(sizeof(CASES)/sizeof(CASES[0]));
    return CASES;
}
