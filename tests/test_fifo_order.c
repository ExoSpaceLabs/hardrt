/* Verifies FIFO wake order within the same priority class when sleepers
 * have different sleep durations. */
#include "test_common.h"

static volatile int g_order[4];
static volatile int g_order_idx = 0;

static void t_sleep_1(void* arg){ (void)arg; for(;;){
        hrt_sleep(1);
        if (g_order_idx < 3){ g_order[++g_order_idx] = 1; }
        if (g_order_idx >= 3){ hrt__test_stop_scheduler(); hrt_yield(); }
        hrt_sleep(1000);
    } }
static void t_sleep_2(void* arg){ (void)arg; for(;;){
        hrt_sleep(2);
        if (g_order_idx < 3){ g_order[++g_order_idx] = 2; }
        if (g_order_idx >= 3){ hrt__test_stop_scheduler(); hrt_yield(); }
        hrt_sleep(1000);
    } }
static void t_sleep_3(void* arg){ (void)arg; for(;;){
        hrt_sleep(3);
        if (g_order_idx < 3){ g_order[++g_order_idx] = 3; }
        if (g_order_idx >= 3){ hrt__test_stop_scheduler(); hrt_yield(); }
        hrt_sleep(1000);
    } }

static void test_wake_fifo_order_same_priority(void){
    hrt__test_reset_scheduler_state();
    for (int i=0;i<4;++i) g_order[i]=0; g_order_idx=0;
    hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=3 };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (fifo order)");

    static uint32_t s1[1024], s2[1024], s3[1024];
    hrt_task_attr_t a = { .priority=HRT_PRIO1, .timeslice=3 };
    int id1 = hrt_create_task(t_sleep_1, NULL, s1, 1024, &a);
    int id2 = hrt_create_task(t_sleep_2, NULL, s2, 1024, &a);
    int id3 = hrt_create_task(t_sleep_3, NULL, s3, 1024, &a);
    T_ASSERT_TRUE(id1>=0 && id2>=0 && id3>=0, "created sleep stagger tasks");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_order[1], "first to wake should be the 1-tick sleeper");
    T_ASSERT_EQ_INT(2, g_order[2], "second to wake should be the 2-tick sleeper");
    T_ASSERT_EQ_INT(3, g_order[3], "third to wake should be the 3-tick sleeper");
}

static const test_case_t CASES[] = {
    {"FIFO: wake order within same priority", test_wake_fifo_order_same_priority},
};

const test_case_t* get_tests_fifo_order(int* out_count){ if(out_count)*out_count=(int)(sizeof(CASES)/sizeof(CASES[0])); return CASES; }
