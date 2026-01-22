#include "test_common.h"
#include "hardrt_queue.h"

/* ---- Case 1: basic try_send/try_recv without blocking ---- */
static void test_queue_try_basic(void) {
    hrt__test_reset_scheduler_state();

    hrt_queue_t q;
    uint32_t storage[4];
    hrt_queue_init(&q, storage, 4, sizeof(uint32_t));

    T_ASSERT_EQ_INT(0, hrt_queue_count(&q), "Initially queue should be empty");

    uint32_t val = 0xDEADBEEF;
    int rc = hrt_queue_try_send(&q, &val);
    T_ASSERT_EQ_INT(0, rc, "try_send should succeed when space is available");
    T_ASSERT_EQ_INT(1, hrt_queue_count(&q), "Queue count should be 1");

    uint32_t out = 0;
    rc = hrt_queue_try_recv(&q, &out);
    T_ASSERT_EQ_INT(0, rc, "try_recv should succeed when item is available");
    T_ASSERT_EQ_INT(0xDEADBEEF, out, "Received value should match sent value");
    T_ASSERT_EQ_INT(0, hrt_queue_count(&q), "Queue count should be 0 after recv");

    /* Fill it up */
    for (uint32_t i = 0; i < 4; i++) {
        rc = hrt_queue_try_send(&q, &i);
        T_ASSERT_EQ_INT(0, rc, "Fill: send should succeed");
    }
    T_ASSERT_EQ_INT(4, hrt_queue_count(&q), "Queue should be full (count=4)");

    val = 99;
    rc = hrt_queue_try_send(&q, &val);
    T_ASSERT_EQ_INT(-1, rc, "try_send should fail when queue is full");

    /* Empty it */
    for (uint32_t i = 0; i < 4; i++) {
        rc = hrt_queue_try_recv(&q, &out);
        T_ASSERT_EQ_INT(0, rc, "Drain: recv should succeed");
        T_ASSERT_EQ_INT(i, out, "Drain: values should be in FIFO order");
    }
    T_ASSERT_EQ_INT(0, hrt_queue_count(&q), "Queue should be empty again");

    rc = hrt_queue_try_recv(&q, &out);
    T_ASSERT_EQ_INT(-1, rc, "try_recv should fail when queue is empty");
}

/* ---- Utility: watchdog to avoid infinite tests ---- */
static volatile int g_watchdog_tripped = 0;
static void watchdog_task(void *arg) {
    uint32_t ticks = (uint32_t) (uintptr_t) arg;
    for (;;) {
        hrt_sleep(ticks);
        g_watchdog_tripped = 1;
        hrt__test_stop_scheduler();
        hrt_yield();
    }
}

/* ---- Case 2: blocking recv wakes when sent ---- */
static volatile int g_recv_val = 0;
static hrt_queue_t g_q_block;

static void t_blocking_receiver(void *arg) {
    (void) arg;
    int val = 0;
    hrt_queue_recv(&g_q_block, &val);
    g_recv_val = val;
    hrt__test_stop_scheduler();
    for(;;) { hrt_yield(); hrt_sleep(1000); }
}

static void t_delayed_sender(void *arg) {
    (void) arg;
    hrt_sleep(20);
    int val = 123;
    hrt_queue_send(&g_q_block, &val);
    for(;;) { hrt_yield(); hrt_sleep(1000); }
}

static void test_queue_block_recv(void) {
    hrt__test_reset_scheduler_state();
    g_recv_val = 0;
    g_watchdog_tripped = 0;

    uint32_t storage[2];
    hrt_queue_init(&g_q_block, storage, 2, sizeof(int));

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t swd[1024], s1[1024], s2[1024];
    hrt_task_attr_t prio_hi = {.priority = HRT_PRIO0, .timeslice = 3};
    hrt_task_attr_t prio_lo = {.priority = HRT_PRIO2, .timeslice = 3};

    hrt_create_task(watchdog_task, (void *) (uintptr_t) 200, swd, 1024, &prio_lo);
    hrt_create_task(t_blocking_receiver, NULL, s1, 1024, &prio_hi);
    hrt_create_task(t_delayed_sender, NULL, s2, 1024, &prio_lo);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "Watchdog should not trip");
    T_ASSERT_EQ_INT(123, g_recv_val, "Receiver should have received 123");
}

/* ---- Case 3: blocking send wakes when received ---- */
static volatile int g_send_finished = 0;

static void t_blocking_sender(void *arg) {
    (void) arg;
    /* Fill the queue first */
    int val = 1;
    hrt_queue_send(&g_q_block, &val);
    val = 2;
    hrt_queue_send(&g_q_block, &val);

    /* This one should block because capacity is 2 */
    val = 3;
    hrt_queue_send(&g_q_block, &val);
    g_send_finished = 1;

    hrt__test_stop_scheduler();
    for(;;) { hrt_yield(); hrt_sleep(1000); }
}

static void t_delayed_receiver(void *arg) {
    (void) arg;
    hrt_sleep(20);
    int out;
    /* Consume one item to make space */
    hrt_queue_recv(&g_q_block, &out);
    T_ASSERT_EQ_INT(1, out, "Should receive first item");
    for(;;) { hrt_yield(); hrt_sleep(1000); }
}

static void test_queue_block_send(void) {
    hrt__test_reset_scheduler_state();
    g_send_finished = 0;
    g_watchdog_tripped = 0;

    uint32_t storage[2];
    hrt_queue_init(&g_q_block, storage, 2, sizeof(int));

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t swd[1024], s1[1024], s2[1024];
    hrt_task_attr_t prio_hi = {.priority = HRT_PRIO0, .timeslice = 3};
    hrt_task_attr_t prio_lo = {.priority = HRT_PRIO2, .timeslice = 3};

    hrt_create_task(watchdog_task, (void *) (uintptr_t) 200, swd, 1024, &prio_lo);
    hrt_create_task(t_blocking_sender, NULL, s1, 1024, &prio_hi);
    hrt_create_task(t_delayed_receiver, NULL, s2, 1024, &prio_lo);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "Watchdog should not trip");
    T_ASSERT_EQ_INT(1, g_send_finished, "Sender should have finished after receiver made space");
}

/* ---- Case 4: FIFO order of waiters ---- */
static volatile int g_fifo_vals[3];
static volatile int g_fifo_count = 0;

static void t_waiter(void *arg) {
    int id = (int)(uintptr_t)arg;
    int val;
    hrt_queue_recv(&g_q_block, &val);
    g_fifo_vals[g_fifo_count++] = id;
    if (g_fifo_count == 3) {
        hrt__test_stop_scheduler();
    }
    for(;;) { hrt_yield(); hrt_sleep(1000); }
}

static void test_queue_fifo_waiters(void) {
    hrt__test_reset_scheduler_state();
    g_fifo_count = 0;
    g_watchdog_tripped = 0;

    uint32_t storage[4];
    hrt_queue_init(&g_q_block, storage, 4, sizeof(int));

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t swd[1024], s1[1024], s2[1024], s3[1024], s4[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 3};

    /* Create waiters. They should block in this order. */
    hrt_create_task(t_waiter, (void*)1, s1, 1024, &a);
    hrt_create_task(t_waiter, (void*)2, s2, 1024, &a);
    hrt_create_task(t_waiter, (void*)3, s3, 1024, &a);

    /* Giver task */
    auto void giver(void* arg) {
        (void)arg;
        hrt_sleep(10);
        int v = 100;
        hrt_queue_send(&g_q_block, &v);
        hrt_sleep(10);
        hrt_queue_send(&g_q_block, &v);
        hrt_sleep(10);
        hrt_queue_send(&g_q_block, &v);
        for(;;) { hrt_yield(); hrt_sleep(1000); }
    }
    hrt_create_task(giver, NULL, s4, 1024, &a);
    hrt_create_task(watchdog_task, (void *) (uintptr_t) 300, swd, 1024, &a);

    hrt_start();

    T_ASSERT_EQ_INT(1, g_fifo_vals[0], "First waiter should be task 1");
    T_ASSERT_EQ_INT(2, g_fifo_vals[1], "Second waiter should be task 2");
    T_ASSERT_EQ_INT(3, g_fifo_vals[2], "Third waiter should be task 3");
}

/* ---- Case 5: ISR variants ---- */
static void test_queue_isr_basic(void) {
    hrt__test_reset_scheduler_state();
    hrt_queue_t q;
    int storage[2];
    hrt_queue_init(&q, storage, 2, sizeof(int));

    int val = 42;
    int need_switch = 0;
    int rc = hrt_queue_try_send_from_isr(&q, &val, &need_switch);
    T_ASSERT_EQ_INT(0, rc, "ISR send should succeed");
    T_ASSERT_EQ_INT(0, need_switch, "No switch needed (no waiters)");

    int out = 0;
    rc = hrt_queue_try_recv_from_isr(&q, &out, &need_switch);
    T_ASSERT_EQ_INT(0, rc, "ISR recv should succeed");
    T_ASSERT_EQ_INT(42, out, "Value matches");
    T_ASSERT_EQ_INT(0, need_switch, "No switch needed");
}

static const test_case_t CASES[] = {
    {"Queue: try_send/recv basic", test_queue_try_basic},
    {"Queue: blocking recv wakes", test_queue_block_recv},
    {"Queue: blocking send wakes", test_queue_block_send},
    {"Queue: FIFO waiter order", test_queue_fifo_waiters},
    {"Queue: ISR variants basic", test_queue_isr_basic},
};

const test_case_t *get_tests_queue(int *out_count) {
    if (out_count) *out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
