#include "test_common.h"
#include "hardrt_queue.h"

/* P2-1 regression: queue wake/preemption decisions must be frozen while the
 * same critical section still protects waiter READY publication. The tests use
 * an external tick so no host SIGALRM timing can influence the result. */

static hrt_queue_t g_q;
static int g_storage[1];
static volatile int g_expect_preempt;
static volatile int g_sender_after;
static volatile int g_receiver_observed_sender_after;
static volatile int g_receiver_ran;
static volatile int g_sender_finished;
static volatile int g_receiver_after;
static volatile int g_sender_observed_receiver_after;
static volatile int g_isr_need_switch;

static hrt_config_t policy_cfg(const hrt_policy_t policy) {
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = policy,
        .default_slice = 10,
        .core_hz = 0,
        .tick_src = HRT_TICK_EXTERNAL
    };
    return cfg;
}

static void reset_queue(void) {
    hrt__test_reset_scheduler_state();
    hrt_queue_init(&g_q, g_storage, 1, sizeof(g_storage[0]));
    g_sender_after = 0;
    g_receiver_observed_sender_after = -1;
    g_receiver_ran = 0;
    g_sender_finished = 0;
    g_receiver_after = 0;
    g_sender_observed_receiver_after = -1;
    g_isr_need_switch = -1;
}

/* ---- send wakes blocked receiver: task-context policy semantics ---- */
static void send_waiting_receiver(void *arg) {
    (void)arg;
    int value = 0;
    (void)hrt_queue_recv(&g_q, &value);
    g_receiver_observed_sender_after = g_sender_after;
    g_receiver_ran = 1;

    if (!g_expect_preempt) {
        hrt__test_stop_scheduler();
        hrt_yield();
    }

    /* No external ticks are injected, so this parks the high-priority task and
     * lets the interrupted lower-priority sender resume. */
    hrt_sleep(1000);
}

static void send_low_task(void *arg) {
    (void)arg;
    int value = 42;
    (void)hrt_queue_try_send(&g_q, &value);
    g_sender_after = 1;

    if (g_expect_preempt) {
        hrt__test_stop_scheduler();
        hrt_yield();
    } else {
        /* Global RR does not allow the newly READY receiver to steal the
         * current quantum. Yield explicitly so the receiver can run. */
        hrt_yield();
    }
}

static void run_send_task_policy(const hrt_policy_t policy, const int expect_preempt) {
    reset_queue();
    g_expect_preempt = expect_preempt;
    const hrt_config_t cfg = policy_cfg(policy);
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "queue send policy init");

    static uint32_t sr[2048], ss[2048];
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 10};
    const hrt_task_attr_t low = {.priority = HRT_PRIO1, .timeslice = 10};
    T_ASSERT_TRUE(hrt_create_task(send_waiting_receiver, NULL, sr, 2048, &high) >= 0 &&
                  hrt_create_task(send_low_task, NULL, ss, 2048, &low) >= 0,
                  "created queue send wake-policy tasks");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_receiver_ran, "blocked receiver was woken");
    T_ASSERT_EQ_INT(1, g_sender_after, "sender eventually continued");
    T_ASSERT_EQ_INT(expect_preempt ? 0 : 1, g_receiver_observed_sender_after,
                    "queue send wake obeys scheduler policy");
}

static void test_queue_send_wake_policy(void) {
    run_send_task_policy(HRT_SCHED_PRIORITY, 1);
    run_send_task_policy(HRT_SCHED_PRIORITY_RR, 1);
    run_send_task_policy(HRT_SCHED_RR, 0);
}

/* ---- receive wakes blocked sender: task-context policy semantics ---- */
static void recv_blocked_sender(void *arg) {
    (void)arg;
    int value = 99;
    (void)hrt_queue_send(&g_q, &value);
    g_sender_observed_receiver_after = g_receiver_after;
    g_sender_finished = 1;

    if (!g_expect_preempt) {
        hrt__test_stop_scheduler();
        hrt_yield();
    }
    hrt_sleep(1000);
}

static void recv_low_task(void *arg) {
    (void)arg;
    int value = 0;
    (void)hrt_queue_try_recv(&g_q, &value);
    g_receiver_after = 1;

    if (g_expect_preempt) {
        hrt__test_stop_scheduler();
        hrt_yield();
    } else {
        hrt_yield();
    }
}

static void run_recv_task_policy(const hrt_policy_t policy, const int expect_preempt) {
    reset_queue();
    g_expect_preempt = expect_preempt;
    const hrt_config_t cfg = policy_cfg(policy);
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "queue receive policy init");

    /* Fill after hrt_init() so the port critical-section state is initialized,
     * but before scheduler start so the high-priority sender blocks first. */
    int initial = 7;
    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_q, &initial), "prefill queue for blocked sender");

    static uint32_t ss[2048], sr[2048];
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 10};
    const hrt_task_attr_t low = {.priority = HRT_PRIO1, .timeslice = 10};
    T_ASSERT_TRUE(hrt_create_task(recv_blocked_sender, NULL, ss, 2048, &high) >= 0 &&
                  hrt_create_task(recv_low_task, NULL, sr, 2048, &low) >= 0,
                  "created queue receive wake-policy tasks");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_sender_finished, "blocked sender was woken and completed");
    T_ASSERT_EQ_INT(1, g_receiver_after, "receiver eventually continued");
    T_ASSERT_EQ_INT(expect_preempt ? 0 : 1, g_sender_observed_receiver_after,
                    "queue receive wake obeys scheduler policy");
}

static void test_queue_recv_wake_policy(void) {
    run_recv_task_policy(HRT_SCHED_PRIORITY, 1);
    run_recv_task_policy(HRT_SCHED_PRIORITY_RR, 1);
    run_recv_task_policy(HRT_SCHED_RR, 0);
}

/* ---- ISR send wakes receiver: need_switch policy semantics ---- */
static void isr_waiting_receiver(void *arg) {
    (void)arg;
    int value = 0;
    (void)hrt_queue_recv(&g_q, &value);
    hrt_sleep(1000);
}

static void isr_send_low_task(void *arg) {
    (void)arg;
    int value = 55;
    int need = -1;
    (void)hrt_queue_try_send_from_isr(&g_q, &value, &need);
    g_isr_need_switch = need;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void run_isr_send_policy(const hrt_policy_t policy, const int expected_need) {
    reset_queue();
    const hrt_config_t cfg = policy_cfg(policy);
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "queue ISR send policy init");

    static uint32_t sr[2048], sg[2048];
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 10};
    const hrt_task_attr_t low = {.priority = HRT_PRIO1, .timeslice = 10};
    T_ASSERT_TRUE(hrt_create_task(isr_waiting_receiver, NULL, sr, 2048, &high) >= 0 &&
                  hrt_create_task(isr_send_low_task, NULL, sg, 2048, &low) >= 0,
                  "created queue ISR send tasks");

    hrt_start();
    T_ASSERT_EQ_INT(expected_need, g_isr_need_switch,
                    "queue ISR send need_switch obeys scheduler policy");
}

static void test_queue_isr_send_policy(void) {
    run_isr_send_policy(HRT_SCHED_PRIORITY, 1);
    run_isr_send_policy(HRT_SCHED_PRIORITY_RR, 1);
    run_isr_send_policy(HRT_SCHED_RR, 0);
}

/* ---- ISR receive wakes sender: need_switch policy semantics ---- */
static void isr_blocked_sender(void *arg) {
    (void)arg;
    int value = 88;
    (void)hrt_queue_send(&g_q, &value);
    hrt_sleep(1000);
}

static void isr_recv_low_task(void *arg) {
    (void)arg;
    int value = 0;
    int need = -1;
    (void)hrt_queue_try_recv_from_isr(&g_q, &value, &need);
    g_isr_need_switch = need;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void run_isr_recv_policy(const hrt_policy_t policy, const int expected_need) {
    reset_queue();
    const hrt_config_t cfg = policy_cfg(policy);
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "queue ISR receive policy init");

    int initial = 11;
    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_q, &initial), "prefill queue for ISR receive");

    static uint32_t ss[2048], sr[2048];
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 10};
    const hrt_task_attr_t low = {.priority = HRT_PRIO1, .timeslice = 10};
    T_ASSERT_TRUE(hrt_create_task(isr_blocked_sender, NULL, ss, 2048, &high) >= 0 &&
                  hrt_create_task(isr_recv_low_task, NULL, sr, 2048, &low) >= 0,
                  "created queue ISR receive tasks");

    hrt_start();
    T_ASSERT_EQ_INT(expected_need, g_isr_need_switch,
                    "queue ISR receive need_switch obeys scheduler policy");
}

static void test_queue_isr_recv_policy(void) {
    run_isr_recv_policy(HRT_SCHED_PRIORITY, 1);
    run_isr_recv_policy(HRT_SCHED_PRIORITY_RR, 1);
    run_isr_recv_policy(HRT_SCHED_RR, 0);
}

static const test_case_t CASES[] = {
    {"Queue wake policy: task send -> receiver", test_queue_send_wake_policy},
    {"Queue wake policy: task receive -> sender", test_queue_recv_wake_policy},
    {"Queue wake policy: ISR send -> receiver", test_queue_isr_send_policy},
    {"Queue wake policy: ISR receive -> sender", test_queue_isr_recv_policy},
};

const test_case_t *get_tests_queue_wake_policy(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
