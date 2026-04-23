#include "test_common.h"

static void test_now_ms_conversion(void) {
    hrt__test_reset_scheduler_state();
    
    // 1000 Hz: 1 tick = 1 ms
    hrt_config_t cfg1 = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR};
    hrt_init(&cfg1);
    
    // Manually advance ticks (since we are not starting the scheduler here, 
    // or we can just test the math)
    T_ASSERT_EQ_UINT(0, hrt_now_ms(), "initial time is 0");
    
    for (int i=0; i<10; ++i) hrt__inc_tick();
    T_ASSERT_EQ_UINT(10, hrt_now_ms(), "10 ticks at 1000Hz = 10ms");

    // 100 Hz: 1 tick = 10 ms
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg2 = {.tick_hz = 100, .policy = HRT_SCHED_PRIORITY_RR};
    hrt_init(&cfg2);
    
    T_ASSERT_EQ_UINT(0, hrt_now_ms(), "initial time is 0 (100Hz)");
    hrt__inc_tick(); // 1 tick
    T_ASSERT_EQ_UINT(10, hrt_now_ms(), "1 tick at 100Hz = 10ms");
    
    for (int i=0; i<9; ++i) hrt__inc_tick(); // total 10 ticks
    T_ASSERT_EQ_UINT(100, hrt_now_ms(), "10 ticks at 100Hz = 100ms");

    // 500 Hz: 1 tick = 2 ms
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg3 = {.tick_hz = 500, .policy = HRT_SCHED_PRIORITY_RR};
    hrt_init(&cfg3);
    T_ASSERT_EQ_UINT(0, hrt_now_ms(), "initial time 0 (500Hz)");
    hrt__inc_tick();
    T_ASSERT_EQ_UINT(2, hrt_now_ms(), "1 tick at 500Hz = 2ms");

    // 123 Hz: non-integer ms per tick
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg4 = {.tick_hz = 123, .policy = HRT_SCHED_PRIORITY_RR};
    hrt_init(&cfg4);
    T_ASSERT_EQ_UINT(0, hrt_now_ms(), "initial time 0 (123Hz)");
    for (int i=0; i<123; ++i) hrt__inc_tick();
    T_ASSERT_EQ_UINT(1000, hrt_now_ms(), "123 ticks at 123Hz = 1000ms");
}

static const test_case_t CASES[] = {
    {"Time: hrt_now_ms conversion", test_now_ms_conversion},
};

const test_case_t *get_tests_now_ms(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
