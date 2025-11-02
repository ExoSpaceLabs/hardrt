#include "test_common.h"

/* Validate that if a task returns, the system remains stable and other tasks run. */
static volatile int g_worker_iters = 0;

static void task_returns_immediately(void *arg) {
    (void) arg; /* just return */
}

static void worker_task(void *arg) {
    (void) arg;
    for (;;) {
        ++g_worker_iters;
        hrt_sleep(1);
        if (g_worker_iters >= 10) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
    }
}

static void test_task_return_does_not_crash(void) {
    hrt__test_reset_scheduler_state();
    g_worker_iters = 0;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (task return)");

    static uint32_t sr[1024], sw[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    int tr = hrt_create_task(task_returns_immediately, NULL, sr, 1024, &a);
    int wk = hrt_create_task(worker_task, NULL, sw, 1024, &a);
    T_ASSERT_TRUE(tr>=0 && wk>=0, "created returner and worker tasks");

    hrt_start();

    T_ASSERT_TRUE(g_worker_iters >= 10, "worker should have progressed even if another task returned");
}

static const test_case_t CASES[] = {
    {"Task: returning entry does not crash scheduler", test_task_return_does_not_crash},
};

const test_case_t *get_tests_task_return(int *out_count) {
    if (out_count)*out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
