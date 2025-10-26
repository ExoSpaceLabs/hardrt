#include "test_common.h"

/* Validate that sleepers waking across 32-bit tick wrap are handled correctly */
static volatile int g_wrap_woke = 0;
static void wrap_sleeper(void* arg){ (void)arg; for(;;){
        /* Sleep for 10 ms at 1 kHz => 10 ticks; wake should compare correctly across wrap */
        hrt_sleep(10);
        g_wrap_woke = 1;
        hrt__test_stop_scheduler();
        hrt_yield();
    } }

static void test_tick_wraparound_sleep_wakes(void){
#ifdef HEARTOS_TEST_HOOKS
    hrt__test_reset_scheduler_state(); g_wrap_woke = 0;
    hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=3 };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (wraparound)");

    /* Place tick near wrap and start a sleeper */
    hrt__test_set_tick(0xFFFFFFF0u);

    static uint32_t st[1024];
    hrt_task_attr_t a = { .priority=HRT_PRIO1, .timeslice=3 };
    int tid = hrt_create_task(wrap_sleeper, NULL, st, 1024, &a);
    T_ASSERT_TRUE(tid>=0, "created wrap sleeper");

    /* Fast-forward enough ticks to cross wrap and reach wake */
    hrt__test_fast_forward_ticks(32);

    /* Enter scheduler to process ready queue and run the woken task */
    hrt_start();

    T_ASSERT_EQ_INT(1, g_wrap_woke, "sleeper should wake across 32-bit tick wrap");
#else
    (void)wrap_sleeper; /* skip if test hooks not present */
    printf("SKIP: wraparound test requires HEARTOS_TEST_HOOKS.\n");
#endif
}

static const test_case_t CASES[] = {
    {"Wrap: sleeper wakes correctly across tick wrap", test_tick_wraparound_sleep_wakes},
};

const test_case_t* get_tests_wraparound(int* out_count){ if(out_count)*out_count=(int)(sizeof(CASES)/sizeof(CASES[0])); return CASES; }
