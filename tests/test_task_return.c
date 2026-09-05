#include "test_common.h"
#include "hardrt_kernel.h"

/* Validate task execution/slot lifecycle independently: RUNNING is observable
 * while dispatched, return/delete transitions to EXITED, and EXITED continues
 * to own its slot until safely reclaimed. */
static volatile int g_worker_iters = 0;
static volatile int g_returner_id = -1;
static volatile int g_returner_running_state = -1;
static volatile int g_delete_id = -1;
static volatile int g_delete_running_state = -1;

static void task_returns_immediately(void *arg) {
    (void) arg;
    g_returner_running_state = hrt__test_task_state(g_returner_id);
    /* just return: the port trampoline converts return into hrt_task_delete() */
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
    g_returner_id = -1;
    g_returner_running_state = -1;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (task return)");

    static uint32_t sr[1024], sw[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    int tr = hrt_create_task(task_returns_immediately, NULL, sr, 1024, &a);
    int wk = hrt_create_task(worker_task, NULL, sw, 1024, &a);
    g_returner_id = tr;
    T_ASSERT_TRUE(tr>=0 && wk>=0, "created returner and worker tasks");

    hrt_start();

    T_ASSERT_TRUE(g_worker_iters >= 10, "worker should have progressed even if another task returned");
    T_ASSERT_EQ_INT(HRT_RUNNING, g_returner_running_state,
                    "dispatched task reports RUNNING rather than READY");
    T_ASSERT_EQ_INT(HRT_EXITED, hrt__test_task_state(tr),
                    "naturally returned task remains observable as EXITED");
    T_ASSERT_EQ_INT(HRT_SLOT_USED, hrt__test_slot_state(tr),
                    "EXITED task still owns its TCB slot");
    T_ASSERT_EQ_INT(0, hrt__test_ready_occurrences(tr),
                    "EXITED task has no READY membership");
}

static void task_calls_delete_explicitly(void *arg) {
    (void) arg;
    g_delete_running_state = hrt__test_task_state(g_delete_id);
    hrt_task_delete();
    /* Should not reach here during normal scheduler operation. */
    for (;;) {
        hrt_yield();
    }
}

static void test_task_explicit_delete(void) {
    hrt__test_reset_scheduler_state();
    g_worker_iters = 0;
    g_delete_id = -1;
    g_delete_running_state = -1;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (task delete)");

    static uint32_t sd[1024], sw[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    int td = hrt_create_task(task_calls_delete_explicitly, NULL, sd, 1024, &a);
    int wk = hrt_create_task(worker_task, NULL, sw, 1024, &a);
    g_delete_id = td;
    T_ASSERT_TRUE(td>=0 && wk>=0, "created delete-calling and worker tasks");

    hrt_start();

    T_ASSERT_TRUE(g_worker_iters >= 10, "worker should have progressed even if another task deleted itself");
    T_ASSERT_EQ_INT(HRT_RUNNING, g_delete_running_state,
                    "explicitly deleted task was RUNNING while executing");
    T_ASSERT_EQ_INT(HRT_EXITED, hrt__test_task_state(td),
                    "explicit delete transitions task to EXITED");
    T_ASSERT_EQ_INT(HRT_SLOT_USED, hrt__test_slot_state(td),
                    "explicitly deleted EXITED task still owns its slot");
    T_ASSERT_EQ_INT(0, hrt__test_ready_occurrences(td),
                    "explicitly deleted task is absent from READY storage");
}

static void replacement_task(void *arg) {
    (void)arg;
    for (;;) hrt_yield();
}

static void test_exited_task_stack_and_slot_reuse(void) {
    hrt__test_reset_scheduler_state();
    g_worker_iters = 0;
    g_returner_id = -1;
    g_returner_running_state = -1;
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 3};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init should return 0 (exited reuse)");

    static uint32_t reusable_stack[1024], worker_stack[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};
    const int original = hrt_create_task(task_returns_immediately, NULL,
                                         reusable_stack, 1024, &a);
    const int worker = hrt_create_task(worker_task, NULL, worker_stack, 1024, &a);
    g_returner_id = original;
    T_ASSERT_TRUE(original >= 0 && worker >= 0,
                  "created exited-slot reuse fixture tasks");

    hrt_start();

    T_ASSERT_EQ_INT(HRT_EXITED, hrt__test_task_state(original),
                    "fixture task is EXITED before reuse");
    T_ASSERT_EQ_INT(HRT_SLOT_USED, hrt__test_slot_state(original),
                    "EXITED fixture still occupies its slot before reuse");

    const int replacement = hrt_create_task(replacement_task, NULL,
                                            reusable_stack, 1024, &a);
    T_ASSERT_EQ_INT(original, replacement,
                    "same stack preferentially reclaims its EXITED owner slot");
    T_ASSERT_EQ_INT(HRT_SLOT_USED, hrt__test_slot_state(replacement),
                    "reclaimed slot remains USED by the replacement task");
    T_ASSERT_EQ_INT(HRT_READY, hrt__test_task_state(replacement),
                    "replacement task is READY after reclaim");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(replacement),
                    "replacement has exactly one READY membership");
}

static const test_case_t CASES[] = {
    {"Task: return transitions RUNNING to EXITED", test_task_return_does_not_crash},
    {"Task: explicit delete transitions RUNNING to EXITED", test_task_explicit_delete},
    {"Task: EXITED stack and slot can be safely reclaimed", test_exited_task_stack_and_slot_reuse},
};

const test_case_t *get_tests_task_return(int *out_count) {
    if (out_count)*out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
