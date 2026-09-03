#include "test_common.h"
#include "hardrt_sem.h"

#ifdef HARDRT_TEST_HOOKS
uint16_t hrt__test_task_slice_left(int id);
#endif

/* ---- Higher-priority wake preserves interrupted PRIORITY_RR task ---- */
static hrt_sem_t g_preempt_sem;
static volatile int g_preempt_high_ran;
static volatile int g_preempt_a_resumed;
static volatile int g_preempt_b_ran_first;
static volatile uint16_t g_preempt_slice_before;
static volatile uint16_t g_preempt_slice_after;
static int g_preempt_a_id;

static void preempt_high_task(void *arg) {
    (void)arg;
    hrt_sem_take(&g_preempt_sem);
    g_preempt_high_ran = 1;
    hrt_sleep(1000);
}

static void preempt_low_a_task(void *arg) {
    (void)arg;

    hrt__test_block_sigalrm();
    hrt__test_fast_forward_ticks(7);
    g_preempt_slice_before = hrt__test_task_slice_left(g_preempt_a_id);

    /* Wakes the higher-priority waiter. The give should hand off to it without
       turning this into an explicit RR yield. */
    hrt_sem_give(&g_preempt_sem);

    g_preempt_slice_after = hrt__test_task_slice_left(g_preempt_a_id);
    g_preempt_a_resumed = 1;
    hrt__test_unblock_sigalrm();

    hrt__test_stop_scheduler();
    hrt_yield();
}

static void preempt_low_b_task(void *arg) {
    (void)arg;
    if (!g_preempt_a_resumed) g_preempt_b_ran_first = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_priority_rr_preemption_retains_current(void) {
    hrt__test_reset_scheduler_state();
    g_preempt_high_ran = 0;
    g_preempt_a_resumed = 0;
    g_preempt_b_ran_first = 0;
    g_preempt_slice_before = 0;
    g_preempt_slice_after = 0;
    hrt_sem_init(&g_preempt_sem, 0);

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 20
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (priority RR preemption)");

    static uint32_t sh[2048], sa[2048], sb[2048];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 0};
    hrt_task_attr_t low = {.priority = HRT_PRIO1, .timeslice = 20};

    int high_id = hrt_create_task(preempt_high_task, NULL, sh, 2048, &high);
    g_preempt_a_id = hrt_create_task(preempt_low_a_task, NULL, sa, 2048, &low);
    int b_id = hrt_create_task(preempt_low_b_task, NULL, sb, 2048, &low);
    T_ASSERT_TRUE(high_id >= 0 && g_preempt_a_id >= 0 && b_id >= 0,
                  "created high, low-A and low-B preemption tasks");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_preempt_high_ran, "higher-priority waiter ran after wake");
    T_ASSERT_EQ_INT(1, g_preempt_a_resumed, "interrupted low-A resumed after high blocked");
    T_ASSERT_EQ_INT(0, g_preempt_b_ran_first, "equal-priority low-B did not pass interrupted low-A");
    T_ASSERT_TRUE(g_preempt_slice_before > 0 && g_preempt_slice_before < 20,
                  "test consumed only part of low-A quantum");
    T_ASSERT_EQ_UINT(g_preempt_slice_before, g_preempt_slice_after,
                     "low-A retained exact remaining quantum across priority preemption");
}

/* ---- Lower-priority ISR wake does not request preemption ---- */
static hrt_sem_t g_lower_sem;
static volatile int g_lower_need_switch;
static volatile int g_lower_woke;

static void lower_waiter_task(void *arg) {
    (void)arg;
    hrt_sem_take(&g_lower_sem);
    g_lower_woke = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void higher_giver_task(void *arg) {
    (void)arg;
    hrt_sleep(5);
    int need = -1;
    hrt_sem_give_from_isr(&g_lower_sem, &need);
    g_lower_need_switch = need;
    hrt_sleep(1);
}

static void test_lower_priority_isr_wake_does_not_preempt(void) {
    hrt__test_reset_scheduler_state();
    g_lower_need_switch = -1;
    g_lower_woke = 0;
    hrt_sem_init(&g_lower_sem, 0);

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (lower-priority wake)");

    static uint32_t sh[2048], sl[2048];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 5};
    hrt_task_attr_t low = {.priority = HRT_PRIO1, .timeslice = 5};

    int high_id = hrt_create_task(higher_giver_task, NULL, sh, 2048, &high);
    int low_id = hrt_create_task(lower_waiter_task, NULL, sl, 2048, &low);
    T_ASSERT_TRUE(high_id >= 0 && low_id >= 0, "created higher giver and lower waiter");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_lower_need_switch, "lower-priority ISR wake does not request preemption");
    T_ASSERT_EQ_INT(1, g_lower_woke, "lower-priority waiter still runs once higher task blocks");
}

/* ---- Equal-priority ISR wake does not steal current RR quantum ---- */
static hrt_sem_t g_equal_sem;
static volatile int g_equal_need_switch;
static volatile int g_equal_giver_continued;
static volatile int g_equal_waiter_woke;

static void equal_waiter_task(void *arg) {
    (void)arg;
    hrt_sem_take(&g_equal_sem);
    g_equal_waiter_woke = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void equal_giver_task(void *arg) {
    (void)arg;
    int need = -1;
    hrt_sem_give_from_isr(&g_equal_sem, &need);
    g_equal_need_switch = need;
    g_equal_giver_continued = 1;
    hrt_sleep(1);
}

static void test_equal_priority_isr_wake_waits_for_current(void) {
    hrt__test_reset_scheduler_state();
    g_equal_need_switch = -1;
    g_equal_giver_continued = 0;
    g_equal_waiter_woke = 0;
    hrt_sem_init(&g_equal_sem, 0);

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 10
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (equal-priority wake)");

    static uint32_t sw[2048], sg[2048];
    hrt_task_attr_t same = {.priority = HRT_PRIO1, .timeslice = 10};

    int waiter_id = hrt_create_task(equal_waiter_task, NULL, sw, 2048, &same);
    int giver_id = hrt_create_task(equal_giver_task, NULL, sg, 2048, &same);
    T_ASSERT_TRUE(waiter_id >= 0 && giver_id >= 0, "created equal-priority waiter and giver");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_equal_need_switch, "equal-priority ISR wake does not request immediate preemption");
    T_ASSERT_EQ_INT(1, g_equal_giver_continued, "current equal-priority task continued until it blocked");
    T_ASSERT_EQ_INT(1, g_equal_waiter_woke, "equal-priority waiter ran after current task blocked");
}

static const test_case_t CASES[] = {
    {"PRIORITY_RR preemption retains current task and quantum", test_priority_rr_preemption_retains_current},
    {"Lower-priority ISR wake does not preempt", test_lower_priority_isr_wake_does_not_preempt},
    {"Equal-priority ISR wake waits for current RR task", test_equal_priority_isr_wake_waits_for_current},
};

const test_case_t *get_tests_preemption_contract(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
