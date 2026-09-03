#include "test_common.h"

#ifdef HARDRT_TEST_HOOKS
uint32_t hrt__test_ready_prio_mask(void);
int hrt__pick_next_ready(void);
#endif

static void noop_task(void *arg) {
    (void)arg;
}

static void test_ready_priority_mask_tracks_fifo_occupancy(void) {
    hrt__test_reset_scheduler_state();
    hrt__test_block_sigalrm();

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (ready priority bitmap)");

    static uint32_t s0[128], s1a[128], s1b[128], s2[128];
    hrt_task_attr_t p0 = {.priority = HRT_PRIO0, .timeslice = 5};
    hrt_task_attr_t p1 = {.priority = HRT_PRIO1, .timeslice = 5};
    hrt_task_attr_t p2 = {.priority = HRT_PRIO2, .timeslice = 5};

    const int id2 = hrt_create_task(noop_task, NULL, s2, 128, &p2);
    const int id1a = hrt_create_task(noop_task, NULL, s1a, 128, &p1);
    const int id0 = hrt_create_task(noop_task, NULL, s0, 128, &p0);
    const int id1b = hrt_create_task(noop_task, NULL, s1b, 128, &p1);

    T_ASSERT_TRUE(id0 >= 0 && id1a >= 0 && id1b >= 0 && id2 >= 0,
                  "created tasks across three priority classes");
    T_ASSERT_EQ_UINT(0x7u, hrt__test_ready_prio_mask(),
                     "mask has one bit per non-empty priority class");

    T_ASSERT_EQ_INT(id0, hrt__pick_next_ready(),
                    "highest-priority ready task selected first");
    T_ASSERT_EQ_UINT(0x6u, hrt__test_ready_prio_mask(),
                     "priority bit clears when its FIFO becomes empty");

    T_ASSERT_EQ_INT(id1a, hrt__pick_next_ready(),
                    "FIFO order preserved within priority class");
    T_ASSERT_EQ_UINT(0x6u, hrt__test_ready_prio_mask(),
                     "priority bit remains while another peer is queued");

    T_ASSERT_EQ_INT(id1b, hrt__pick_next_ready(),
                    "second equal-priority task follows first");
    T_ASSERT_EQ_UINT(0x4u, hrt__test_ready_prio_mask(),
                     "priority bit clears after last peer leaves FIFO");

    T_ASSERT_EQ_INT(id2, hrt__pick_next_ready(),
                    "lowest populated priority selected after higher classes empty");
    T_ASSERT_EQ_UINT(0u, hrt__test_ready_prio_mask(),
                     "mask is empty after all ready FIFOs drain");

    hrt__test_unblock_sigalrm();
}

static const test_case_t CASES[] = {
    {"Ready priority bitmap tracks FIFO occupancy", test_ready_priority_mask_tracks_fifo_occupancy},
};

const test_case_t *get_tests_ready_bitmap(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
