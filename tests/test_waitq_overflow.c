/* SPDX-License-Identifier: Apache-2.0 */
#include "test_common.h"
#include "hardrt_port_int.h"
#include "hardrt_sem.h"
#include "hardrt_mutex.h"
#include "hardrt_queue.h"

#define STACK_WORDS 1024

static uint32_t g_probe_stack[STACK_WORDS];
static uint32_t g_watchdog_stack[STACK_WORDS];

static volatile int g_probe_rc;
static volatile int g_probe_finished;
static volatile int g_probe_state_after;
static volatile int g_wait_count_after;
static volatile int g_watchdog_ran;
static int g_probe_id;

static hrt_sem_t g_sem;
static hrt_mutex_t g_mutex;
static hrt_queue_t g_queue;
static int g_queue_storage[1];

static hrt_config_t external_cfg(void) {
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = HRT_SCHED_PRIORITY;
    cfg.default_slice = 0;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    return cfg;
}

static void reset_result(void) {
    g_probe_rc = -99;
    g_probe_finished = 0;
    g_probe_state_after = -99;
    g_wait_count_after = -99;
    g_watchdog_ran = 0;
    g_probe_id = -1;
}

static void finish_probe(const int rc, const int wait_count) {
    const int me = hrt__get_current();
    const _hrt_tcb_t *t = hrt__tcb(me);

    g_probe_rc = rc;
    g_probe_state_after = t ? (int)t->state : -99;
    g_wait_count_after = wait_count;
    g_probe_finished = 1;

    hrt__test_stop_scheduler();
    hrt_yield();
}

/* If the probe is incorrectly stranded as BLOCKED, this task becomes runnable
 * next and terminates the test rather than letting CI hang forever. */
static void overflow_watchdog(void *arg) {
    (void)arg;
    g_watchdog_ran = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static int run_probe(hrt_task_fn probe) {
    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0 };

    g_probe_id = hrt_create_task(probe, NULL,
                                 g_probe_stack, STACK_WORDS, &attr);
    const int watchdog_id = hrt_create_task(overflow_watchdog, NULL,
                                             g_watchdog_stack, STACK_WORDS, &attr);
    T_ASSERT_TRUE(g_probe_id >= 0 && watchdog_id >= 0,
                  "created overflow probe and watchdog");
    if (g_probe_id < 0 || watchdog_id < 0) return -1;

    hrt_start();
    return 0;
}

static void assert_fail_safe(const char *context) {
    (void)context;
    T_ASSERT_EQ_INT(1, g_probe_finished,
                    "overflow probe returned instead of being stranded");
    T_ASSERT_EQ_INT(0, g_watchdog_ran,
                    "overflow watchdog did not need to recover a blocked probe");
    T_ASSERT_EQ_INT(-1, g_probe_rc,
                    "blocking API reports waiter insertion failure");
    T_ASSERT_EQ_INT(HRT_RUNNING, g_probe_state_after,
                    "caller remains RUNNING when waiter insertion fails before blocking");
    T_ASSERT_EQ_INT(HARDRT_APP_MAX_TASKS, g_wait_count_after,
                    "full waiter count was not modified on insertion failure");
    if (g_probe_id >= 0) {
        const _hrt_tcb_t *t = hrt__tcb(g_probe_id);
        T_ASSERT_TRUE(t != NULL && t->state == HRT_READY,
                      "probe TCB is READY after scheduler returns");
    }
}

static void sem_overflow_probe(void *arg) {
    (void)arg;
    const int rc = hrt_sem_take(&g_sem);
    finish_probe(rc, (int)g_sem.count_wait);
}

static void test_sem_waiter_overflow_does_not_block(void) {
    hrt__test_reset_scheduler_state();
    reset_result();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init semaphore waiter-overflow test");

    hrt_sem_init(&g_sem, 0);
    g_sem.count_wait = HARDRT_APP_MAX_TASKS;

    if (run_probe(sem_overflow_probe) == 0) {
        assert_fail_safe("semaphore");
    }
}

static void mutex_overflow_probe(void *arg) {
    (void)arg;
    const int rc = hrt_mutex_lock(&g_mutex);
    finish_probe(rc, (int)g_mutex.count_wait);
}

static void test_mutex_waiter_overflow_does_not_block(void) {
    hrt__test_reset_scheduler_state();
    reset_result();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init mutex waiter-overflow test");

    hrt_mutex_init(&g_mutex);
    g_mutex.locked = 1u;
    g_mutex.owner = HRT_MUTEX_NO_OWNER;
    g_mutex.count_wait = HARDRT_APP_MAX_TASKS;

    if (run_probe(mutex_overflow_probe) == 0) {
        assert_fail_safe("mutex");
    }
}

static void queue_recv_overflow_probe(void *arg) {
    (void)arg;
    int out = 0;
    const int rc = hrt_queue_recv(&g_queue, &out);
    finish_probe(rc, (int)g_queue.rx_wait);
}

static void test_queue_recv_waiter_overflow_does_not_block(void) {
    hrt__test_reset_scheduler_state();
    reset_result();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init queue RX waiter-overflow test");

    hrt_queue_init(&g_queue, g_queue_storage, 1, sizeof(g_queue_storage[0]));
    g_queue.rx_wait = HARDRT_APP_MAX_TASKS;

    if (run_probe(queue_recv_overflow_probe) == 0) {
        assert_fail_safe("queue receive");
    }
}

static void queue_send_overflow_probe(void *arg) {
    (void)arg;
    const int value = 22;
    const int rc = hrt_queue_send(&g_queue, &value);
    finish_probe(rc, (int)g_queue.tx_wait);
}

static void test_queue_send_waiter_overflow_does_not_block(void) {
    hrt__test_reset_scheduler_state();
    reset_result();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init queue TX waiter-overflow test");

    hrt_queue_init(&g_queue, g_queue_storage, 1, sizeof(g_queue_storage[0]));
    const int initial = 11;
    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_queue, &initial),
                    "prefilled queue before TX overflow probe");
    g_queue.tx_wait = HARDRT_APP_MAX_TASKS;

    if (run_probe(queue_send_overflow_probe) == 0) {
        assert_fail_safe("queue send");
    }
}

static const test_case_t CASES[] = {
    {"Semaphore waiter overflow never strands caller", test_sem_waiter_overflow_does_not_block},
    {"Mutex waiter overflow never strands caller", test_mutex_waiter_overflow_does_not_block},
    {"Queue RX waiter overflow never strands caller", test_queue_recv_waiter_overflow_does_not_block},
    {"Queue TX waiter overflow never strands caller", test_queue_send_waiter_overflow_does_not_block},
};

const test_case_t *get_tests_waitq_overflow(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
