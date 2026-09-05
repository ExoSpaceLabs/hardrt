/* Deterministic tests for shared event flags. */
#include "test_common.h"
#include "hardrt_event.h"

static volatile int g_event_watchdog = 0;

static void event_watchdog(void *arg) {
    const uint32_t delay = (uint32_t)(uintptr_t)arg;
    hrt_sleep(delay);
    g_event_watchdog = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static hrt_config_t event_cfg(hrt_policy_t policy) {
    hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = policy,
        .default_slice = 3u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_SYSTICK
    };
    return cfg;
}

static void test_event_basic_bits_and_validation(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = event_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "event basic: init kernel");

    hrt_event_t event;
    hrt_event_init(&event);
    T_ASSERT_EQ_UINT(0u, hrt_event_get(&event), "event starts with no bits");
    T_ASSERT_EQ_INT(0, hrt_event_set(&event, 0x5u), "event set succeeds");
    T_ASSERT_EQ_UINT(0x5u, hrt_event_get(&event), "event set ORs bits");
    T_ASSERT_EQ_INT(0, hrt_event_clear(&event, 0x1u), "event clear succeeds");
    T_ASSERT_EQ_UINT(0x4u, hrt_event_get(&event), "event clear removes selected bits");
    T_ASSERT_EQ_INT(0, hrt_event_set(&event, 0u), "zero set is a valid no-op");
    T_ASSERT_EQ_UINT(0x4u, hrt_event_get(&event), "zero set preserves bits");

    hrt_event_bits_t matched = 0xFFFFFFFFu;
    T_ASSERT_EQ_INT(-1, hrt_event_wait(&event, 0u, HRT_EVENT_WAIT_ANY, &matched),
                    "zero wait mask is rejected");
    T_ASSERT_EQ_UINT(0u, matched, "invalid wait clears matched output");
    T_ASSERT_EQ_INT(-1, hrt_event_wait(&event, 1u, 0x80u, NULL),
                    "unknown wait options are rejected");
    T_ASSERT_EQ_INT(-1, hrt_event_set(NULL, 1u), "null event set is rejected");
    T_ASSERT_EQ_INT(-1, hrt_event_clear(NULL, 1u), "null event clear is rejected");
}

static hrt_event_t g_event_any;
static volatile uint32_t g_event_any_match = 0u;

static void event_any_waiter(void *arg) {
    (void)arg;
    hrt_event_bits_t matched = 0u;
    if (hrt_event_wait(&g_event_any, 0x3u, HRT_EVENT_CLEAR_ON_EXIT, &matched) == 0) {
        g_event_any_match = matched;
    }
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void event_any_setter(void *arg) {
    (void)arg;
    hrt_sleep(10u);
    hrt_event_set(&g_event_any, 0x2u);
    hrt_sleep(1000u);
}

static void test_event_wait_any_clear_on_exit(void) {
    hrt__test_reset_scheduler_state();
    hrt_event_init(&g_event_any);
    g_event_any_match = 0u;
    g_event_watchdog = 0;

    hrt_config_t cfg = event_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "event any: init kernel");

    static uint32_t waiter_stack[1024], setter_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    T_ASSERT_TRUE(hrt_create_task(event_any_waiter, NULL, waiter_stack, 1024, &high) >= 0,
                  "event any: create waiter");
    T_ASSERT_TRUE(hrt_create_task(event_any_setter, NULL, setter_stack, 1024, &low) >= 0,
                  "event any: create setter");
    T_ASSERT_TRUE(hrt_create_task(event_watchdog, (void *)(uintptr_t)200u,
                                  watchdog_stack, 1024, &low) >= 0,
                  "event any: create watchdog");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_event_watchdog, "event any: watchdog did not trip");
    T_ASSERT_EQ_UINT(0x2u, g_event_any_match, "wait-any returns matching subset");
    T_ASSERT_EQ_UINT(0u, hrt_event_get(&g_event_any), "clear-on-exit clears matched bit");
}

static hrt_event_t g_event_all;
static volatile uint32_t g_event_all_match = 0u;

static void event_all_waiter(void *arg) {
    (void)arg;
    hrt_event_bits_t matched = 0u;
    if (hrt_event_wait(&g_event_all, 0x3u, HRT_EVENT_WAIT_ALL, &matched) == 0) {
        g_event_all_match = matched;
    }
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void event_all_setter(void *arg) {
    (void)arg;
    hrt_sleep(5u);
    hrt_event_set(&g_event_all, 0x1u);
    hrt_sleep(5u);
    hrt_event_set(&g_event_all, 0x2u);
    hrt_sleep(1000u);
}

static void test_event_wait_all_incremental_retained(void) {
    hrt__test_reset_scheduler_state();
    hrt_event_init(&g_event_all);
    g_event_all_match = 0u;
    g_event_watchdog = 0;

    hrt_config_t cfg = event_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "event all: init kernel");

    static uint32_t waiter_stack[1024], setter_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    hrt_create_task(event_all_waiter, NULL, waiter_stack, 1024, &high);
    hrt_create_task(event_all_setter, NULL, setter_stack, 1024, &low);
    hrt_create_task(event_watchdog, (void *)(uintptr_t)200u, watchdog_stack, 1024, &low);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_event_watchdog, "event all: watchdog did not trip");
    T_ASSERT_EQ_UINT(0x3u, g_event_all_match, "wait-all wakes only after complete mask");
    T_ASSERT_EQ_UINT(0x3u, hrt_event_get(&g_event_all), "retained event bits remain set");
}

static hrt_event_t g_event_multi;
static volatile uint32_t g_event_multi_match[2];
static volatile int g_event_multi_order[2];
static volatile int g_event_multi_count = 0;

static void event_multi_waiter(void *arg) {
    const int index = (int)(uintptr_t)arg;
    const hrt_event_bits_t mask = index == 0 ? 0x1u : 0x3u;
    hrt_event_bits_t matched = 0u;
    if (hrt_event_wait(&g_event_multi, mask, HRT_EVENT_CLEAR_ON_EXIT, &matched) == 0) {
        g_event_multi_match[index] = matched;
        const int order = g_event_multi_count++;
        if (order < 2) g_event_multi_order[order] = index + 1;
    }
    if (g_event_multi_count == 2) hrt__test_stop_scheduler();
    hrt_yield();
}

static void event_multi_setter(void *arg) {
    (void)arg;
    hrt_sleep(10u);
    hrt_event_set(&g_event_multi, 0x3u);
    hrt_sleep(1000u);
}

static void test_event_multi_waiter_common_snapshot_fifo(void) {
    hrt__test_reset_scheduler_state();
    hrt_event_init(&g_event_multi);
    memset((void *)g_event_multi_match, 0, sizeof(g_event_multi_match));
    memset((void *)g_event_multi_order, 0, sizeof(g_event_multi_order));
    g_event_multi_count = 0;
    g_event_watchdog = 0;

    hrt_config_t cfg = event_cfg(HRT_SCHED_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "event multi: init kernel");

    static uint32_t w1_stack[1024], w2_stack[1024], setter_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t same = {.priority = HRT_PRIO1, .timeslice = 2u};
    hrt_create_task(event_multi_waiter, (void *)(uintptr_t)0u, w1_stack, 1024, &same);
    hrt_create_task(event_multi_waiter, (void *)(uintptr_t)1u, w2_stack, 1024, &same);
    hrt_create_task(event_multi_setter, NULL, setter_stack, 1024, &same);
    hrt_create_task(event_watchdog, (void *)(uintptr_t)200u, watchdog_stack, 1024, &same);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_event_watchdog, "event multi: watchdog did not trip");
    T_ASSERT_EQ_INT(2, g_event_multi_count, "one set wakes both matching waiters");
    T_ASSERT_EQ_UINT(0x1u, g_event_multi_match[0], "first waiter sees its snapshot match");
    T_ASSERT_EQ_UINT(0x3u, g_event_multi_match[1], "second waiter sees same pre-clear snapshot");
    T_ASSERT_EQ_INT(1, g_event_multi_order[0], "global RR publishes first waiter first");
    T_ASSERT_EQ_INT(2, g_event_multi_order[1], "global RR preserves waiter FIFO order");
    T_ASSERT_EQ_UINT(0u, hrt_event_get(&g_event_multi), "union of clear-on-exit matches is cleared once");
}

static hrt_event_t g_event_isr;
static volatile int g_event_isr_need = 0;
static volatile uint32_t g_event_isr_match = 0u;

static void event_isr_waiter(void *arg) {
    (void)arg;
    hrt_event_bits_t matched = 0u;
    if (hrt_event_wait(&g_event_isr, 0x8u, HRT_EVENT_WAIT_ANY, &matched) == 0) {
        g_event_isr_match = matched;
    }
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void event_isr_setter(void *arg) {
    (void)arg;
    hrt_sleep(10u);
    int need = 0;
    hrt_event_set_from_isr(&g_event_isr, 0x8u, &need);
    g_event_isr_need = need;
    hrt_yield();
    hrt_sleep(1000u);
}

static void test_event_set_from_isr_need_switch(void) {
    hrt__test_reset_scheduler_state();
    hrt_event_init(&g_event_isr);
    g_event_isr_need = 0;
    g_event_isr_match = 0u;
    g_event_watchdog = 0;

    hrt_config_t cfg = event_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "event ISR: init kernel");

    static uint32_t waiter_stack[1024], setter_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    hrt_create_task(event_isr_waiter, NULL, waiter_stack, 1024, &high);
    hrt_create_task(event_isr_setter, NULL, setter_stack, 1024, &low);
    hrt_create_task(event_watchdog, (void *)(uintptr_t)200u, watchdog_stack, 1024, &low);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_event_watchdog, "event ISR: watchdog did not trip");
    T_ASSERT_EQ_INT(1, g_event_isr_need, "ISR set reports higher-priority wake");
    T_ASSERT_EQ_UINT(0x8u, g_event_isr_match, "ISR set wakes matching waiter");
}

static const test_case_t CASES[] = {
    {"Event: basic bits and validation", test_event_basic_bits_and_validation},
    {"Event: wait-any clear-on-exit", test_event_wait_any_clear_on_exit},
    {"Event: wait-all incremental retained", test_event_wait_all_incremental_retained},
    {"Event: common snapshot and FIFO multi-wake", test_event_multi_waiter_common_snapshot_fifo},
    {"Event: ISR set need_switch", test_event_set_from_isr_need_switch}
};

const test_case_t *get_tests_event(int *out_count) {
    if (out_count != NULL) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
