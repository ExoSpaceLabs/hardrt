/* SPDX-License-Identifier: Apache-2.0 */
#include "test_common.h"
#include "hardrt_queue.h"

#define STACK_WORDS 1024

static hrt_queue_t g_q;
static int g_storage[1];
static uint32_t g_stack_a[STACK_WORDS];
static uint32_t g_stack_mid[STACK_WORDS];
static uint32_t g_stack_b[STACK_WORDS];

static volatile int g_a_done;
static volatile int g_a_value;
static volatile int g_a_rc;
static volatile int g_mid_value;
static volatile int g_b_value;
static volatile int g_b_barged_before_a;
static volatile int g_b_send_rc;

static hrt_config_t rr_cfg(void) {
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = HRT_SCHED_RR;
    cfg.default_slice = 0;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    return cfg;
}

static void reset_fixture(void) {
    g_a_done = 0;
    g_a_value = -1;
    g_a_rc = -99;
    g_mid_value = -1;
    g_b_value = -1;
    g_b_barged_before_a = 0;
    g_b_send_rc = -99;
}

/* RX contract:
 * A blocks first. Mid enqueues item 11 and therefore selects/wakes A. Under
 * global RR A does not preempt Mid and is appended behind already-ready B.
 * B consumes item 11 before A resumes, then enqueues 22. A retries its
 * blocking receive and completes with 22. */
static void rx_waiter_a(void *arg) {
    (void)arg;
    int value = -1;
    g_a_rc = hrt_queue_recv(&g_q, &value);
    g_a_value = value;
    g_a_done = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void rx_middle_sender(void *arg) {
    (void)arg;
    const int first = 11;
    (void)hrt_queue_send(&g_q, &first);
    hrt_yield();
}

static void rx_barger(void *arg) {
    (void)arg;
    int first = -1;
    if (hrt_queue_try_recv(&g_q, &first) == 0) {
        g_b_value = first;
        g_b_barged_before_a = (g_a_done == 0);
    }

    const int second = 22;
    (void)hrt_queue_try_send(&g_q, &second);
    hrt_yield();
}

static void test_rx_waiter_selection_does_not_reserve_item(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = rr_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init RX barging contract test");
    hrt_queue_init(&g_q, g_storage, 1, sizeof(g_storage[0]));

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0 };
    T_ASSERT_TRUE(hrt_create_task(rx_waiter_a, NULL, g_stack_a, STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(rx_middle_sender, NULL, g_stack_mid, STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(rx_barger, NULL, g_stack_b, STACK_WORDS, &attr) >= 0,
                  "created RX waiter, sender and barger");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_b_barged_before_a,
                    "ready receiver may consume item before selected waiter resumes");
    T_ASSERT_EQ_INT(11, g_b_value,
                    "barging receiver consumed the first queued item");
    T_ASSERT_EQ_INT(0, g_a_rc,
                    "selected receiver eventually completed after retry");
    T_ASSERT_EQ_INT(22, g_a_value,
                    "selected receiver consumed the later item after retry");
    T_ASSERT_EQ_INT(1, g_a_done, "selected receiver completed");
}

/* TX contract:
 * Queue starts full with 11. A blocks trying to send 22. Mid removes 11 and
 * therefore selects/wakes A, but global RR leaves already-ready B ahead of A.
 * B uses the newly freed slot for 33 before A resumes, then removes 33 again.
 * A retries and eventually sends 22. */
static void tx_waiter_a(void *arg) {
    (void)arg;
    const int value = 22;
    g_a_rc = hrt_queue_send(&g_q, &value);
    g_a_done = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void tx_middle_receiver(void *arg) {
    (void)arg;
    int value = -1;
    (void)hrt_queue_recv(&g_q, &value);
    g_mid_value = value;
    hrt_yield();
}

static void tx_barger(void *arg) {
    (void)arg;
    const int value = 33;
    g_b_send_rc = hrt_queue_try_send(&g_q, &value);
    g_b_barged_before_a = (g_b_send_rc == 0 && g_a_done == 0);

    int out = -1;
    if (hrt_queue_try_recv(&g_q, &out) == 0) g_b_value = out;
    hrt_yield();
}

static void test_tx_waiter_selection_does_not_reserve_capacity(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = rr_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init TX barging contract test");
    hrt_queue_init(&g_q, g_storage, 1, sizeof(g_storage[0]));

    const int initial = 11;
    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_q, &initial),
                    "prefilled queue for TX waiter contract");

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0 };
    T_ASSERT_TRUE(hrt_create_task(tx_waiter_a, NULL, g_stack_a, STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(tx_middle_receiver, NULL, g_stack_mid, STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(tx_barger, NULL, g_stack_b, STACK_WORDS, &attr) >= 0,
                  "created TX waiter, receiver and barger");

    hrt_start();

    T_ASSERT_EQ_INT(11, g_mid_value,
                    "middle receiver freed the original queue slot");
    T_ASSERT_EQ_INT(1, g_b_barged_before_a,
                    "ready sender may use capacity before selected waiter resumes");
    T_ASSERT_EQ_INT(0, g_b_send_rc,
                    "barging sender used the newly available slot");
    T_ASSERT_EQ_INT(33, g_b_value,
                    "barging sender's item was present and removed");
    T_ASSERT_EQ_INT(0, g_a_rc,
                    "selected sender eventually completed after retry");
    T_ASSERT_EQ_INT(1, g_a_done, "selected sender completed");

    int final = -1;
    T_ASSERT_EQ_INT(0, hrt_queue_try_recv(&g_q, &final),
                    "selected sender's retried item remains queued");
    T_ASSERT_EQ_INT(22, final,
                    "selected sender eventually queued its original item");
}

static const test_case_t CASES[] = {
    {"Queue RX wake selection permits retry barging", test_rx_waiter_selection_does_not_reserve_item},
    {"Queue TX wake selection permits retry barging", test_tx_waiter_selection_does_not_reserve_capacity},
};

const test_case_t *get_tests_queue_barging(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
