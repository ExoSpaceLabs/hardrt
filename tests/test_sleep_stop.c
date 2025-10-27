/* Exercises sleep/wake timing and deterministic scheduler stop via test hook. */
#include "test_common.h"

static volatile int g_wake_count = 0;
static int g_target_wakes = 5;
static uint32_t g_start_tick = 0;

static void sleeper_task(void* arg){
    (void)arg;
    for(;;){
        hrt_sleep(10); /* 10 ms */
        ++g_wake_count;
        if (g_wake_count >= g_target_wakes){
            /* Request scheduler stop and yield back to scheduler */
            hrt__test_stop_scheduler();
            hrt_yield();
        }
    }
}

static void test_sleep_wake_and_stop(void){
    /* Config: 1 kHz tick, RR policy, default slice 5 */
    hrt_config_t cfg = { .tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5 };
    int rc = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, rc, "hrt_init should return 0");

    static uint32_t stack_sleeper[2048];
    hrt_task_attr_t attr = { .priority = HRT_PRIO1, .timeslice = 5 };
    int tid = hrt_create_task(sleeper_task, NULL, stack_sleeper, sizeof(stack_sleeper)/sizeof(stack_sleeper[0]), &attr);
    T_ASSERT_TRUE(tid >= 0, "task created");

    g_start_tick = hrt_tick_now();

    /* Enter scheduler; it will return after sleeper task requests stop. */
    hrt_start();

    /* Back from scheduler: verify wake count and that time advanced */
    T_ASSERT_EQ_INT(g_target_wakes, g_wake_count, "sleeper should have woken desired number of times");
    uint32_t elapsed = hrt_tick_now() - g_start_tick;
    T_ASSERT_TRUE(elapsed >= (uint32_t)(g_target_wakes * 10), "tick advanced at least expected amount");
}

static const test_case_t CASES[] = {
    {"Sleep/Wake and Stop", test_sleep_wake_and_stop},
};

const test_case_t* get_tests_sleep_stop(int* out_count){
    if (out_count) *out_count = (int)(sizeof(CASES)/sizeof(CASES[0]));
    return CASES;
}
