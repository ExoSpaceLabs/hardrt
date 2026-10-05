/* SPDX-License-Identifier: Apache-2.0 */
#include "test_common.h"
#include "hardrt_port_int.h"
#include "hardrt_sem.h"
#include "hardrt_mutex.h"
#include "hardrt_queue.h"
#include "hardrt_event.h"
#include "hardrt_notify.h"

#define STACK_WORDS 1024

static uint32_t g_stack_a[STACK_WORDS];
static uint32_t g_stack_b[STACK_WORDS];
static uint32_t g_stack_c[STACK_WORDS];
static uint32_t g_stack_d[STACK_WORDS];

static hrt_sem_t g_sem;
static hrt_mutex_t g_mutex;
static hrt_queue_t g_queue;
static int g_queue_storage[1];
static hrt_event_t g_event;

static volatile hrt_wait_result_t g_result_a;
static volatile hrt_wait_result_t g_result_b;
static volatile hrt_tick_t g_observed_tick;
static volatile int g_done_count;
static volatile int g_task_id;
static volatile uint32_t g_notify_value;
static volatile hrt_event_bits_t g_event_matched;
static volatile int g_barged;
static volatile int g_barger_value;
static volatile int g_middle_value;

static hrt_config_t external_cfg(const hrt_policy_t policy) {
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000u;
    cfg.policy = policy;
    cfg.default_slice = 0u;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    return cfg;
}

static void reset_fixture(void) {
    g_result_a = HRT_WAIT_ERROR;
    g_result_b = HRT_WAIT_ERROR;
    g_observed_tick = UINT32_MAX;
    g_done_count = 0;
    g_task_id = -1;
    g_notify_value = UINT32_MAX;
    g_event_matched = 0u;
    g_barged = 0;
    g_barger_value = -1;
    g_middle_value = -1;
    hrt_sem_init(&g_sem, 0u);
    hrt_mutex_init(&g_mutex);
    hrt_queue_init(&g_queue, g_queue_storage, 1u, sizeof(g_queue_storage[0]));
    hrt_event_init(&g_event);
}

static void stop_from_task(void) {
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void tick_driver(void *arg) {
    const int ticks = (int)(uintptr_t)arg;
    for (int i = 0; i < ticks; ++i) {
        hrt_tick_from_isr();
        hrt_yield();
    }
    stop_from_task();
}

static void immediate_contract_task(void *arg) {
    (void)arg;
    const hrt_tick_t now = hrt_tick_now();

    hrt_sem_init(&g_sem, 1u);
    T_ASSERT_EQ_INT(HRT_WAIT_OK, hrt_sem_take_until(&g_sem, now),
                    "due semaphore deadline succeeds when token is available");
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, hrt_sem_take_until(&g_sem, now),
                    "due semaphore deadline becomes non-blocking timeout when empty");

    const int first = 11;
    const int second = 22;
    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_queue, &first),
                    "prefill queue for immediate receive");
    int out = -1;
    T_ASSERT_EQ_INT(HRT_WAIT_OK, hrt_queue_recv_until(&g_queue, &out, now),
                    "due queue receive succeeds when data exists");
    T_ASSERT_EQ_INT(11, out, "immediate timed receive returns queued item");
    T_ASSERT_EQ_INT(HRT_WAIT_OK, hrt_queue_send_until(&g_queue, &first, now),
                    "due queue send succeeds when capacity exists");
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, hrt_queue_send_until(&g_queue, &second, now),
                    "due queue send times out when full");

    hrt_mutex_init(&g_mutex);
    T_ASSERT_EQ_INT(HRT_WAIT_OK, hrt_mutex_lock_until(&g_mutex, now),
                    "due mutex deadline succeeds when unlocked");
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&g_mutex),
                    "unlock immediate timed mutex");

    hrt_event_init(&g_event);
    T_ASSERT_EQ_INT(0, hrt_event_set(&g_event, 0x4u),
                    "seed event bit for immediate wait");
    g_event_matched = 0u;
    T_ASSERT_EQ_INT(HRT_WAIT_OK,
                    hrt_event_wait_until(&g_event, 0x4u,
                                         HRT_EVENT_CLEAR_ON_EXIT,
                                         now, (hrt_event_bits_t *)&g_event_matched),
                    "due event wait succeeds when condition already matches");
    T_ASSERT_EQ_UINT(0x4u, g_event_matched,
                     "immediate timed event reports matched bit");
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_event_wait_until(&g_event, 0x4u,
                                         HRT_EVENT_WAIT_ANY,
                                         now, NULL),
                    "due event wait times out when condition is absent");

    const int me = hrt__get_current();
    T_ASSERT_EQ_INT(0, hrt_task_notify(me, 0x55u, HRT_NOTIFY_OVERWRITE),
                    "seed notification for immediate wait");
    uint32_t value = 0u;
    T_ASSERT_EQ_INT(HRT_WAIT_OK,
                    hrt_task_notify_wait_until(0u, UINT32_MAX, now, &value),
                    "due notification wait consumes existing pending value");
    T_ASSERT_EQ_UINT(0x55u, value,
                     "immediate timed notification wait returns value");
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_task_notify_wait_until(0u, 0u, now, &value),
                    "due notification wait times out when nothing is pending");

    T_ASSERT_EQ_INT(0, hrt_task_notify(me, 0u, HRT_NOTIFY_INCREMENT),
                    "seed counting notification");
    value = 0u;
    T_ASSERT_EQ_INT(HRT_WAIT_OK,
                    hrt_task_notify_take_until(1, now, &value),
                    "due notification take consumes existing count");
    T_ASSERT_EQ_UINT(1u, value,
                     "immediate timed notification take returns count");
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_task_notify_take_until(1, now, &value),
                    "due notification take times out at zero count");

    stop_from_task();
}

static void test_immediate_deadline_contract(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init immediate timed-wait contract");
    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(immediate_contract_task, NULL, g_stack_a,
                                  STACK_WORDS, &attr) >= 0,
                  "created immediate timed-wait contract task");
    hrt_start();
}

static void test_timed_waits_reject_non_task_context(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init invalid-context timeout test");

    int item = 7;
    uint32_t value = 0u;
    hrt_event_bits_t matched = 0u;
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_CONTEXT,
                    hrt_sem_take_until(&g_sem, 1u),
                    "timed semaphore wait is task-context only");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_CONTEXT,
                    hrt_queue_send_until(&g_queue, &item, 1u),
                    "timed queue send is task-context only");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_CONTEXT,
                    hrt_queue_recv_until(&g_queue, &item, 1u),
                    "timed queue receive is task-context only");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_CONTEXT,
                    hrt_mutex_lock_until(&g_mutex, 1u),
                    "timed mutex lock is task-context only");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_CONTEXT,
                    hrt_event_wait_until(&g_event, 1u, HRT_EVENT_WAIT_ANY,
                                         1u, &matched),
                    "timed event wait is task-context only");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_CONTEXT,
                    hrt_task_notify_wait_until(0u, 0u, 1u, &value),
                    "timed notification wait is task-context only");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_CONTEXT,
                    hrt_task_notify_take_until(1, 1u, &value),
                    "timed notification take is task-context only");
}

static void ambiguous_contract_task(void *arg) {
    (void)arg;
    const hrt_tick_t ambiguous = hrt_tick_now() + HRT_TICK_HALF_RANGE;
    int item = 1;
    uint32_t value = 0u;

    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_DEADLINE,
                    hrt_sem_take_until(&g_sem, ambiguous),
                    "semaphore rejects exact half-range deadline");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_DEADLINE,
                    hrt_queue_recv_until(&g_queue, &item, ambiguous),
                    "queue receive rejects exact half-range deadline");

    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_queue, &item),
                    "fill queue before ambiguous send");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_DEADLINE,
                    hrt_queue_send_until(&g_queue, &item, ambiguous),
                    "queue send rejects exact half-range deadline");

    g_mutex.locked = 1u;
    g_mutex.owner = HRT_MUTEX_NO_OWNER;
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_DEADLINE,
                    hrt_mutex_lock_until(&g_mutex, ambiguous),
                    "mutex rejects exact half-range deadline");

    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_DEADLINE,
                    hrt_event_wait_until(&g_event, 1u, HRT_EVENT_WAIT_ANY,
                                         ambiguous, NULL),
                    "event wait rejects exact half-range deadline");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_DEADLINE,
                    hrt_task_notify_wait_until(0u, 0u, ambiguous, &value),
                    "notification wait rejects exact half-range deadline");
    T_ASSERT_EQ_INT(HRT_WAIT_INVALID_DEADLINE,
                    hrt_task_notify_take_until(1, ambiguous, &value),
                    "notification take rejects exact half-range deadline");

    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "ambiguous semaphore deadline publishes no waiter");
    T_ASSERT_EQ_INT(0, g_queue.rx_wait,
                    "ambiguous queue RX deadline publishes no waiter");
    T_ASSERT_EQ_INT(0, g_queue.tx_wait,
                    "ambiguous queue TX deadline publishes no waiter");
    T_ASSERT_EQ_INT(0, g_mutex.count_wait,
                    "ambiguous mutex deadline publishes no waiter");
    T_ASSERT_EQ_INT(0, g_event.wait_count,
                    "ambiguous event deadline publishes no waiter");

    stop_from_task();
}

static void test_half_range_deadline_rejected_by_all_timed_waits(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init half-range timeout contract");
    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(ambiguous_contract_task, NULL, g_stack_a,
                                  STACK_WORDS, &attr) >= 0,
                  "created half-range timeout contract task");
    hrt_start();
}

static void sem_timeout_waiter(void *arg) {
    const hrt_tick_t deadline = (hrt_tick_t)(uintptr_t)arg;
    g_result_a = hrt_sem_take_until(&g_sem, deadline);
    g_observed_tick = hrt_tick_now();
    stop_from_task();
}

static void run_sem_timeout_policy(const hrt_policy_t policy, const char *label) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(policy);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), label);

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(sem_timeout_waiter, (void *)(uintptr_t)5u,
                                  g_stack_a, STACK_WORDS, &waiter) >= 0,
                  "created timed semaphore waiter");
    T_ASSERT_TRUE(hrt_create_task(tick_driver, (void *)(uintptr_t)8u,
                                  g_stack_b, STACK_WORDS, &driver) >= 0,
                  "created timed semaphore tick driver");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, g_result_a,
                    "semaphore wait expires at absolute deadline");
    T_ASSERT_EQ_UINT(5u, g_observed_tick,
                     "semaphore timeout resumes at nominal deadline");
    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "timed-out semaphore waiter is unlinked");
}

static void test_sem_timeout_fixed_priority(void) {
    run_sem_timeout_policy(HRT_SCHED_PRIORITY,
                           "init fixed-priority semaphore timeout");
}
static void test_sem_timeout_global_rr(void) {
    run_sem_timeout_policy(HRT_SCHED_RR,
                           "init global-RR semaphore timeout");
}
static void test_sem_timeout_priority_rr(void) {
    run_sem_timeout_policy(HRT_SCHED_PRIORITY_RR,
                           "init PRIORITY_RR semaphore timeout");
}

static void test_sem_timeout_wrap(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init wrapped semaphore timeout");
    hrt__test_set_tick(0xFFFFFFFCu);

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(sem_timeout_waiter, (void *)(uintptr_t)1u,
                                  g_stack_a, STACK_WORDS, &waiter) >= 0 &&
                  hrt_create_task(tick_driver, (void *)(uintptr_t)8u,
                                  g_stack_b, STACK_WORDS, &driver) >= 0,
                  "created wrapped semaphore timeout tasks");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, g_result_a,
                    "semaphore timeout survives uint32 wrap");
    T_ASSERT_EQ_UINT(1u, g_observed_tick,
                     "wrapped timeout expires at absolute deadline");
    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "wrapped timeout removes semaphore waiter");
}

static void sem_multi_waiter(void *arg) {
    const int marker = (int)(uintptr_t)arg;
    const hrt_wait_result_t result = hrt_sem_take_until(&g_sem, 5u);
    if (marker == 1) g_result_a = result;
    else g_result_b = result;
    g_done_count++;
    if (g_done_count >= 2) stop_from_task();
    else hrt_yield();
}

static void test_simultaneous_timeout_expiry(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init simultaneous timeout expiry");

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(sem_multi_waiter, (void *)(uintptr_t)1u,
                                  g_stack_a, STACK_WORDS, &waiter) >= 0 &&
                  hrt_create_task(sem_multi_waiter, (void *)(uintptr_t)2u,
                                  g_stack_b, STACK_WORDS, &waiter) >= 0 &&
                  hrt_create_task(tick_driver, (void *)(uintptr_t)8u,
                                  g_stack_c, STACK_WORDS, &driver) >= 0,
                  "created simultaneous timeout tasks");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, g_result_a,
                    "first same-deadline waiter timed out");
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, g_result_b,
                    "second same-deadline waiter timed out");
    T_ASSERT_EQ_INT(2, g_done_count,
                    "all same-deadline timeout waiters resumed");
    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "simultaneous expiry unlinks every semaphore waiter");
}

static void sem_success_before_boundary_driver(void *arg) {
    (void)arg;
    for (int i = 0; i < 4; ++i) hrt_tick_from_isr();
    (void)hrt_sem_give(&g_sem);
    hrt_tick_from_isr();
    hrt_yield();
}

static void test_producer_wins_before_timeout_boundary(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init producer-before-timeout test");

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(sem_timeout_waiter, (void *)(uintptr_t)5u,
                                  g_stack_a, STACK_WORDS, &waiter) >= 0 &&
                  hrt_create_task(sem_success_before_boundary_driver, NULL,
                                  g_stack_b, STACK_WORDS, &driver) >= 0,
                  "created producer-before-timeout tasks");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_OK, g_result_a,
                    "producer serialized before expiry wins wait");
    T_ASSERT_EQ_UINT(4u, g_observed_tick,
                     "producer wake disarms timeout before deadline tick");
    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "successful wake removes semaphore waiter");
    T_ASSERT_EQ_INT(0, g_sem.count,
                    "successful waiter receives token directly");
}

static void sem_timeout_before_producer_driver(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; ++i) hrt_tick_from_isr();
    (void)hrt_sem_give(&g_sem);
    hrt_yield();
}

static void test_timeout_wins_at_boundary_before_producer(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init timeout-before-producer test");

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t driver = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(sem_timeout_waiter, (void *)(uintptr_t)5u,
                                  g_stack_a, STACK_WORDS, &waiter) >= 0 &&
                  hrt_create_task(sem_timeout_before_producer_driver, NULL,
                                  g_stack_b, STACK_WORDS, &driver) >= 0,
                  "created timeout-before-producer tasks");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, g_result_a,
                    "expiry serialized before producer wins boundary race");
    T_ASSERT_EQ_UINT(5u, g_observed_tick,
                     "timeout winner resumes at deadline");
    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "timeout winner is absent from semaphore waiter queue");
    T_ASSERT_EQ_INT(1, g_sem.count,
                    "post-timeout give becomes an available token");
}

static void long_mutex_holder(void *arg) {
    (void)arg;
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&g_mutex),
                    "long-lived holder acquired mutex");
    hrt_sleep(1000u);
}

static void all_timeout_worker(void *arg) {
    (void)arg;
    hrt_tick_t deadline;

    deadline = hrt_tick_now() + 2u;
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, hrt_sem_take_until(&g_sem, deadline),
                    "semaphore future wait timed out");
    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "semaphore timeout removed waiter");

    int value = -1;
    deadline = hrt_tick_now() + 2u;
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_queue_recv_until(&g_queue, &value, deadline),
                    "queue receive future wait timed out");
    T_ASSERT_EQ_INT(0, g_queue.rx_wait,
                    "queue receive timeout removed waiter");

    const int initial = 11;
    const int second = 22;
    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_queue, &initial),
                    "filled queue before send timeout");
    deadline = hrt_tick_now() + 2u;
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_queue_send_until(&g_queue, &second, deadline),
                    "queue send future wait timed out");
    T_ASSERT_EQ_INT(0, g_queue.tx_wait,
                    "queue send timeout removed waiter");

    deadline = hrt_tick_now() + 2u;
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_mutex_lock_until(&g_mutex, deadline),
                    "mutex future wait timed out");
    T_ASSERT_EQ_INT(0, g_mutex.count_wait,
                    "mutex timeout removed waiter");

    deadline = hrt_tick_now() + 2u;
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_event_wait_until(&g_event, 0x1u,
                                         HRT_EVENT_WAIT_ANY,
                                         deadline, NULL),
                    "event future wait timed out");
    T_ASSERT_EQ_INT(0, g_event.wait_count,
                    "event timeout removed waiter");

    uint32_t observed = 0u;
    deadline = hrt_tick_now() + 2u;
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_task_notify_wait_until(0u, 0u, deadline, &observed),
                    "notification-value future wait timed out");
    T_ASSERT_EQ_UINT(0u, observed,
                     "timed-out notification wait returns no value");

    _hrt_tcb_t *const self = hrt__tcb(hrt__get_current());
    T_ASSERT_TRUE(self != NULL && self->notify_waiting == 0u,
                  "notification timeout clears waiter state");

    deadline = hrt_tick_now() + 2u;
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT,
                    hrt_task_notify_take_until(1, deadline, &observed),
                    "notification-take future wait timed out");
    T_ASSERT_TRUE(self != NULL && self->notify_waiting == 0u,
                  "notification-take timeout clears waiter state");

    stop_from_task();
}

static void test_all_primitive_timeout_unlink_paths(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init all-primitive timeout test");

    const hrt_task_attr_t holder = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t worker = { .priority = HRT_PRIO1, .timeslice = 0u };
    const hrt_task_attr_t driver = { .priority = HRT_PRIO2, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(long_mutex_holder, NULL, g_stack_a,
                                  STACK_WORDS, &holder) >= 0 &&
                  hrt_create_task(all_timeout_worker, NULL, g_stack_b,
                                  STACK_WORDS, &worker) >= 0 &&
                  hrt_create_task(tick_driver, (void *)(uintptr_t)30u,
                                  g_stack_c, STACK_WORDS, &driver) >= 0,
                  "created all-primitive timeout tasks");
    hrt_start();
}

static void mutex_short_holder(void *arg) {
    (void)arg;
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&g_mutex),
                    "short holder acquired mutex");
    hrt_sleep(3u);
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&g_mutex),
                    "short holder released mutex");
}

static void mutex_success_waiter(void *arg) {
    (void)arg;
    g_result_a = hrt_mutex_lock_until(&g_mutex, 10u);
    if (g_result_a == HRT_WAIT_OK) {
        T_ASSERT_EQ_INT(hrt__get_current(), g_mutex.owner,
                        "timed mutex wake transfers ownership directly");
        (void)hrt_mutex_unlock(&g_mutex);
    }
    stop_from_task();
}

static void test_mutex_success_before_deadline(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init timed mutex success");

    const hrt_task_attr_t holder = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t waiter = { .priority = HRT_PRIO1, .timeslice = 0u };
    const hrt_task_attr_t driver = { .priority = HRT_PRIO2, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(mutex_short_holder, NULL, g_stack_a,
                                  STACK_WORDS, &holder) >= 0 &&
                  hrt_create_task(mutex_success_waiter, NULL, g_stack_b,
                                  STACK_WORDS, &waiter) >= 0 &&
                  hrt_create_task(tick_driver, (void *)(uintptr_t)12u,
                                  g_stack_c, STACK_WORDS, &driver) >= 0,
                  "created timed mutex success tasks");
    hrt_start();
    T_ASSERT_EQ_INT(HRT_WAIT_OK, g_result_a,
                    "mutex waiter succeeded before deadline");
}

static void event_success_waiter(void *arg) {
    (void)arg;
    g_event_matched = 0u;
    g_result_a = hrt_event_wait_until(&g_event, 0x3u, HRT_EVENT_WAIT_ALL,
                                      10u,
                                      (hrt_event_bits_t *)&g_event_matched);
    stop_from_task();
}

static void event_success_producer(void *arg) {
    (void)arg;
    hrt_tick_from_isr();
    hrt_tick_from_isr();
    (void)hrt_event_set(&g_event, 0x3u);
    hrt_yield();
}

static void test_event_success_before_deadline(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init timed event success");

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t producer = { .priority = HRT_PRIO1, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(event_success_waiter, NULL, g_stack_a,
                                  STACK_WORDS, &waiter) >= 0 &&
                  hrt_create_task(event_success_producer, NULL, g_stack_b,
                                  STACK_WORDS, &producer) >= 0,
                  "created timed event success tasks");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_OK, g_result_a,
                    "event waiter succeeded before deadline");
    T_ASSERT_EQ_UINT(0x3u, g_event_matched,
                     "timed event waiter received matched bits");
    T_ASSERT_EQ_INT(0, g_event.wait_count,
                    "successful event wake removed waiter");
}

static void notify_wait_success_waiter(void *arg) {
    (void)arg;
    g_task_id = hrt__get_current();
    g_notify_value = 0u;
    g_result_a = hrt_task_notify_wait_until(0u, 0u, 10u,
                                            (uint32_t *)&g_notify_value);
    stop_from_task();
}

static void notify_wait_success_producer(void *arg) {
    (void)arg;
    hrt_tick_from_isr();
    hrt_tick_from_isr();
    (void)hrt_task_notify(g_task_id, 0x1234u, HRT_NOTIFY_OVERWRITE);
    hrt_yield();
}

static void test_notification_wait_success_before_deadline(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init notification wait success");

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t producer = { .priority = HRT_PRIO1, .timeslice = 0u };
    const int waiter_id = hrt_create_task(notify_wait_success_waiter, NULL,
                                          g_stack_a, STACK_WORDS, &waiter);
    g_task_id = waiter_id;
    T_ASSERT_TRUE(waiter_id >= 0 &&
                  hrt_create_task(notify_wait_success_producer, NULL,
                                  g_stack_b, STACK_WORDS, &producer) >= 0,
                  "created notification wait success tasks");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_OK, g_result_a,
                    "notification wait succeeded before deadline");
    T_ASSERT_EQ_UINT(0x1234u, g_notify_value,
                     "timed notification wait returned producer value");
}

static void notify_take_success_waiter(void *arg) {
    (void)arg;
    g_task_id = hrt__get_current();
    g_notify_value = 0u;
    g_result_a = hrt_task_notify_take_until(1, 10u,
                                            (uint32_t *)&g_notify_value);
    stop_from_task();
}

static void notify_take_success_producer(void *arg) {
    (void)arg;
    hrt_tick_from_isr();
    hrt_tick_from_isr();
    (void)hrt_task_notify(g_task_id, 0u, HRT_NOTIFY_INCREMENT);
    hrt_yield();
}

static void test_notification_take_success_before_deadline(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_PRIORITY);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init notification take success");

    const hrt_task_attr_t waiter = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t producer = { .priority = HRT_PRIO1, .timeslice = 0u };
    const int waiter_id = hrt_create_task(notify_take_success_waiter, NULL,
                                          g_stack_a, STACK_WORDS, &waiter);
    g_task_id = waiter_id;
    T_ASSERT_TRUE(waiter_id >= 0 &&
                  hrt_create_task(notify_take_success_producer, NULL,
                                  g_stack_b, STACK_WORDS, &producer) >= 0,
                  "created notification take success tasks");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_OK, g_result_a,
                    "notification take succeeded before deadline");
    T_ASSERT_EQ_UINT(1u, g_notify_value,
                     "timed notification take returned incremented count");
}

static void timed_rx_waiter(void *arg) {
    (void)arg;
    int value = -1;
    g_result_a = hrt_queue_recv_until(&g_queue, &value, 100u);
    g_barger_value = value;
    stop_from_task();
}

static void timed_rx_middle_sender(void *arg) {
    (void)arg;
    const int value = 11;
    (void)hrt_queue_send(&g_queue, &value);
    hrt_yield();
}

static void timed_rx_barger(void *arg) {
    (void)arg;
    int first = -1;
    if (hrt_queue_try_recv(&g_queue, &first) == 0) {
        g_barged = 1;
        g_middle_value = first;
    }
    hrt_yield();

    const int second = 22;
    (void)hrt_queue_try_send(&g_queue, &second);
    hrt_yield();
}

static void test_timed_queue_receive_rearms_after_barging(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init timed RX barging test");

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(timed_rx_waiter, NULL, g_stack_a,
                                  STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(timed_rx_middle_sender, NULL, g_stack_b,
                                  STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(timed_rx_barger, NULL, g_stack_c,
                                  STACK_WORDS, &attr) >= 0,
                  "created timed RX barging tasks");
    hrt_start();

    T_ASSERT_EQ_INT(1, g_barged,
                    "barger consumed first item before timed waiter resumed");
    T_ASSERT_EQ_INT(11, g_middle_value,
                    "barger consumed first queued item");
    T_ASSERT_EQ_INT(HRT_WAIT_OK, g_result_a,
                    "timed receive rearmed and eventually succeeded");
    T_ASSERT_EQ_INT(22, g_barger_value,
                    "timed receive consumed later item after rearm");
    T_ASSERT_EQ_INT(0, g_queue.rx_wait,
                    "timed receive leaves no stale waiter after rearm");
}

static void timed_tx_waiter(void *arg) {
    (void)arg;
    const int value = 22;
    g_result_a = hrt_queue_send_until(&g_queue, &value, 100u);
    stop_from_task();
}

static void timed_tx_middle_receiver(void *arg) {
    (void)arg;
    int value = -1;
    (void)hrt_queue_recv(&g_queue, &value);
    g_middle_value = value;
    hrt_yield();
}

static void timed_tx_barger(void *arg) {
    (void)arg;
    const int value = 33;
    if (hrt_queue_try_send(&g_queue, &value) == 0) g_barged = 1;
    hrt_yield();

    int out = -1;
    if (hrt_queue_try_recv(&g_queue, &out) == 0) g_barger_value = out;
    hrt_yield();
}

static void test_timed_queue_send_rearms_after_barging(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = external_cfg(HRT_SCHED_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init timed TX barging test");

    const int initial = 11;
    T_ASSERT_EQ_INT(0, hrt_queue_try_send(&g_queue, &initial),
                    "prefilled queue for timed TX barging");

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(timed_tx_waiter, NULL, g_stack_a,
                                  STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(timed_tx_middle_receiver, NULL, g_stack_b,
                                  STACK_WORDS, &attr) >= 0 &&
                  hrt_create_task(timed_tx_barger, NULL, g_stack_c,
                                  STACK_WORDS, &attr) >= 0,
                  "created timed TX barging tasks");
    hrt_start();

    T_ASSERT_EQ_INT(11, g_middle_value,
                    "middle receiver removed original item");
    T_ASSERT_EQ_INT(1, g_barged,
                    "barger used capacity before timed sender resumed");
    T_ASSERT_EQ_INT(33, g_barger_value,
                    "barger later removed its own item");
    T_ASSERT_EQ_INT(HRT_WAIT_OK, g_result_a,
                    "timed send rearmed and eventually succeeded");
    T_ASSERT_EQ_INT(0, g_queue.tx_wait,
                    "timed send leaves no stale waiter after rearm");

    int final = -1;
    T_ASSERT_EQ_INT(0, hrt_queue_try_recv(&g_queue, &final),
                    "timed sender's item remains queued");
    T_ASSERT_EQ_INT(22, final,
                    "timed sender preserved original item across rearm");
}

static void internal_tick_sem_waiter(void *arg) {
    (void)arg;
    const hrt_tick_t deadline = hrt_tick_now() + 3u;
    g_result_a = hrt_sem_take_until(&g_sem, deadline);
    g_observed_tick = hrt_tick_now();
    stop_from_task();
}

static void test_internal_tick_drives_ipc_timeout(void) {
    hrt__test_reset_scheduler_state();
    reset_fixture();
    const hrt_config_t cfg = {
        .tick_hz = 100u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 0u,
        .tick_src = HRT_TICK_SYSTICK
    };
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "init internal-tick IPC timeout");

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(internal_tick_sem_waiter, NULL, g_stack_a,
                                  STACK_WORDS, &attr) >= 0,
                  "created internal-tick semaphore waiter");
    hrt_start();

    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, g_result_a,
                    "internal tick expires IPC deadline");
    T_ASSERT_EQ_UINT(3u, g_observed_tick,
                     "internal tick resumes timed waiter at deadline");
    T_ASSERT_EQ_INT(0, g_sem.count_wait,
                    "internal-tick timeout unlinks semaphore waiter");
}

static const test_case_t CASES[] = {
    {"IPC timeout: due deadline has try semantics", test_immediate_deadline_contract},
    {"IPC timeout: timed waits reject non-task context", test_timed_waits_reject_non_task_context},
    {"IPC timeout: exact half-range is rejected everywhere", test_half_range_deadline_rejected_by_all_timed_waits},
    {"IPC timeout: fixed-priority semaphore expiry", test_sem_timeout_fixed_priority},
    {"IPC timeout: global-RR semaphore expiry", test_sem_timeout_global_rr},
    {"IPC timeout: PRIORITY_RR semaphore expiry", test_sem_timeout_priority_rr},
    {"IPC timeout: semaphore deadline survives tick wrap", test_sem_timeout_wrap},
    {"IPC timeout: simultaneous expiries wake every waiter", test_simultaneous_timeout_expiry},
    {"IPC timeout: producer before boundary wins", test_producer_wins_before_timeout_boundary},
    {"IPC timeout: expiry before producer wins boundary", test_timeout_wins_at_boundary_before_producer},
    {"IPC timeout: all primitive expiry paths unlink", test_all_primitive_timeout_unlink_paths},
    {"IPC timeout: mutex succeeds before deadline", test_mutex_success_before_deadline},
    {"IPC timeout: event succeeds before deadline", test_event_success_before_deadline},
    {"IPC timeout: notification wait succeeds before deadline", test_notification_wait_success_before_deadline},
    {"IPC timeout: notification take succeeds before deadline", test_notification_take_success_before_deadline},
    {"IPC timeout: timed queue receive rearms after barging", test_timed_queue_receive_rearms_after_barging},
    {"IPC timeout: timed queue send rearms after barging", test_timed_queue_send_rearms_after_barging},
    {"IPC timeout: internal tick drives expiry", test_internal_tick_drives_ipc_timeout},
};

const test_case_t *get_tests_ipc_timeout(int *out_count) {
    if (out_count != NULL) {
        *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    }
    return CASES;
}
