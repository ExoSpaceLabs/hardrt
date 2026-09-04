#include "test_common.h"
#include "hardrt_kernel.h"

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

static void wake_race_dummy_task(void *arg) {
    (void)arg;
}

static void exercise_wake_before_reschedule(hrt_policy_t policy, hrt_state_t blocked_state) {
    hrt__test_reset_scheduler_state();
    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = policy,
        .default_slice = 1,
        .tick_src = HRT_TICK_SYSTICK
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "wake-race init");

    /* Keep the hosted tick from perturbing this white-box scheduler boundary.
       The Cortex-M race being modeled is an IRQ wake after the task published a
       blocking state but before PendSV consumed the outgoing context. */
    hrt__test_block_sigalrm();

    static uint32_t stack[128];
    const hrt_task_attr_t attr = {.priority = HRT_PRIO0, .timeslice = 1};
    const int id = hrt_create_task(wake_race_dummy_task, NULL, stack, 128, &attr);
    T_ASSERT_TRUE(id >= 0, "wake-race task created");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(id),
                    "new task has exactly one READY entry");
    T_ASSERT_EQ_INT(1, hrt__test_task_ready_queued(id),
                    "new task READY membership marker is set");

    const int selected = hrt__pick_next_ready();
    T_ASSERT_EQ_INT(id, selected, "wake-race task selected as current");
    hrt__set_current(id);
    T_ASSERT_EQ_INT(0, hrt__test_ready_occurrences(id),
                    "running task is absent from READY storage");
    T_ASSERT_EQ_INT(0, hrt__test_task_ready_queued(id),
                    "running task READY membership marker is clear");

    _hrt_tcb_t *const t = hrt__tcb(id);
    T_ASSERT_TRUE(t != NULL, "wake-race TCB exists");
    if (t != NULL) {
        /* Simulate the exact vulnerable window: the still-running task publishes
         * a transient pending block/sleep state, an IRQ wakes it, then PendSV
         * finally consumes the outgoing context. */
        hrt__block_current(blocked_state);
        hrt__make_ready(id);

        T_ASSERT_EQ_INT(HRT_READY, t->state,
                        "IRQ wake restores logical READY state");
        T_ASSERT_EQ_INT(0, hrt__test_ready_occurrences(id),
                        "racing wake leaves still-running task out of READY storage");
        T_ASSERT_EQ_INT(0, hrt__test_task_ready_queued(id),
                        "racing wake does not pre-enqueue current task");

        hrt__prepare_current_for_reschedule();

        T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(id),
                        "scheduler preparation enqueues raced task exactly once");
        T_ASSERT_EQ_INT(1, hrt__test_task_ready_queued(id),
                        "scheduler establishes authoritative READY membership");

        T_ASSERT_EQ_INT(id, hrt__pick_next_ready(),
                        "raced task remains dispatchable exactly once");
        T_ASSERT_EQ_INT(0, hrt__test_task_ready_queued(id),
                        "dispatch clears READY membership marker");
        T_ASSERT_EQ_INT(0, hrt__test_ready_occurrences(id),
                        "no duplicate READY entry remains after dispatch");
    }

    hrt__test_unblock_sigalrm();
}

static void test_wake_before_reschedule_keeps_single_ready_membership(void) {
    const hrt_policy_t policies[] = {
        HRT_SCHED_PRIORITY,
        HRT_SCHED_RR,
        HRT_SCHED_PRIORITY_RR
    };
    const hrt_state_t blocked_states[] = {
        HRT_BLOCKED,
        HRT_SLEEP
    };

    for (size_t p = 0u; p < sizeof(policies) / sizeof(policies[0]); ++p) {
        for (size_t s = 0u; s < sizeof(blocked_states) / sizeof(blocked_states[0]); ++s) {
            printf("[wake-race] policy=%d outgoing_state=%d\n",
                   (int)policies[p], (int)blocked_states[s]);
            exercise_wake_before_reschedule(policies[p], blocked_states[s]);
        }
    }
}

static void test_completed_block_does_not_suppress_later_requeue(void) {
    hrt__test_reset_scheduler_state();
    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 1,
        .tick_src = HRT_TICK_SYSTICK
    };
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "late-wake init");
    hrt__test_block_sigalrm();

    static uint32_t stack[128];
    const hrt_task_attr_t attr = {.priority = HRT_PRIO0, .timeslice = 1};
    const int id = hrt_create_task(wake_race_dummy_task, NULL, stack, 128, &attr);
    T_ASSERT_TRUE(id >= 0, "late-wake task created");
    T_ASSERT_EQ_INT(id, hrt__pick_next_ready(), "late-wake task selected as current");
    hrt__set_current(id);

    _hrt_tcb_t *const t = hrt__tcb(id);
    T_ASSERT_TRUE(t != NULL, "late-wake TCB exists");
    if (t != NULL) {
        /* Scheduler entry closes the vulnerable window by normalizing the
         * transient pending state into the stable blocked state. */
        hrt__block_current(HRT_BLOCKED);
        T_ASSERT_EQ_INT(HRT_BLOCKED_PENDING, t->state,
                        "block publication uses transient pending state");
        hrt__prepare_current_for_reschedule();
        T_ASSERT_EQ_INT(HRT_BLOCKED, t->state,
                        "scheduler boundary normalizes completed block");
        T_ASSERT_EQ_INT(0, hrt__test_ready_occurrences(id),
                        "completed block has no READY membership");

        /* A later ordinary wake must enqueue immediately. */
        hrt__make_ready(id);
        T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(id),
                        "late wake creates exactly one READY entry");
        T_ASSERT_EQ_INT(id, hrt__pick_next_ready(),
                        "late-woken task dispatches normally");
        hrt__set_current(id);

        hrt__prepare_current_for_reschedule();
        T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(id),
                        "later normal preemption requeues current task");
        T_ASSERT_EQ_INT(id, hrt__pick_next_ready(),
                        "requeued late-woken task remains dispatchable");
    }

    hrt__test_unblock_sigalrm();
}

static const test_case_t CASES[] = {
    {"Ready priority bitmap tracks scheduler FIFO occupancy", test_ready_priority_mask_tracks_fifo_occupancy},
    {"Wake before reschedule preserves single READY membership", test_wake_before_reschedule_keeps_single_ready_membership},
    {"Completed block does not suppress later requeue", test_completed_block_does_not_suppress_later_requeue},
};

const test_case_t *get_tests_ready_bitmap(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
