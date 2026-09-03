#include "test_common.h"

#ifdef HARDRT_TEST_HOOKS
uint32_t hrt__test_ready_prio_mask(void);
#endif

static volatile uint32_t g_masks[4];
static volatile uint32_t g_sequence[4];
static volatile uint32_t g_steps;

static void record_step(uint32_t tag) {
    const uint32_t i = g_steps;
    if (i < 4u) {
        g_sequence[i] = tag;
        g_masks[i] = hrt__test_ready_prio_mask();
        g_steps = i + 1u;
    }
}

static void task_p0(void *arg) {
    (void)arg;
    record_step(0u);
    hrt_task_delete();
}

static void task_p1a(void *arg) {
    (void)arg;
    record_step(1u);
    hrt_task_delete();
}

static void task_p1b(void *arg) {
    (void)arg;
    record_step(2u);
    hrt_task_delete();
}

static void task_p2_stop(void *arg) {
    (void)arg;
    record_step(3u);
    hrt__test_stop_scheduler();
}

static void test_ready_priority_mask_tracks_fifo_occupancy(void) {
    hrt__test_reset_scheduler_state();
    g_steps = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        g_masks[i] = 0xFFFFFFFFu;
        g_sequence[i] = 0xFFFFFFFFu;
    }

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (ready priority bitmap)");

    static uint32_t s0[128], s1a[128], s1b[128], s2[128];
    const hrt_task_attr_t p0 = {.priority = HRT_PRIO0, .timeslice = 0};
    const hrt_task_attr_t p1 = {.priority = HRT_PRIO1, .timeslice = 0};
    const hrt_task_attr_t p2 = {.priority = HRT_PRIO2, .timeslice = 0};

    /* Create in deliberately non-priority order. The scheduler, not creation
       order, must choose p0 first and preserve FIFO within p1. */
    const int id2 = hrt_create_task(task_p2_stop, NULL, s2, 128, &p2);
    const int id1a = hrt_create_task(task_p1a, NULL, s1a, 128, &p1);
    const int id0 = hrt_create_task(task_p0, NULL, s0, 128, &p0);
    const int id1b = hrt_create_task(task_p1b, NULL, s1b, 128, &p1);

    T_ASSERT_TRUE(id0 >= 0 && id1a >= 0 && id1b >= 0 && id2 >= 0,
                  "created tasks across three priority classes");
    T_ASSERT_EQ_UINT(0x7u, hrt__test_ready_prio_mask(),
                     "mask has one bit per non-empty priority class before start");

    hrt_start();

    T_ASSERT_EQ_UINT(4u, g_steps, "all four scheduler steps were observed");
    T_ASSERT_EQ_UINT(0u, g_sequence[0], "p0 dispatched first");
    T_ASSERT_EQ_UINT(1u, g_sequence[1], "first p1 task preserves FIFO order");
    T_ASSERT_EQ_UINT(2u, g_sequence[2], "second p1 task follows first");
    T_ASSERT_EQ_UINT(3u, g_sequence[3], "p2 dispatched after higher classes drain");

    /* The currently executing task has already been removed from its ready FIFO.
       The mask therefore describes the remaining queued READY tasks. */
    T_ASSERT_EQ_UINT(0x6u, g_masks[0],
                     "p0 bit clears when p0 becomes current");
    T_ASSERT_EQ_UINT(0x6u, g_masks[1],
                     "p1 bit remains while equal-priority peer is queued");
    T_ASSERT_EQ_UINT(0x4u, g_masks[2],
                     "p1 bit clears after its last queued peer is dispatched");
    T_ASSERT_EQ_UINT(0u, g_masks[3],
                     "mask is empty when final queued task becomes current");
}

static const test_case_t CASES[] = {
    {"Ready priority bitmap tracks scheduler FIFO occupancy", test_ready_priority_mask_tracks_fifo_occupancy},
};

const test_case_t *get_tests_ready_bitmap(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
