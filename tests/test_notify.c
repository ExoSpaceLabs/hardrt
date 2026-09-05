/* Deterministic tests for lightweight per-task notifications. */
#include "test_common.h"
#include "hardrt_notify.h"
#include "hardrt_sem.h"

static volatile int g_notify_watchdog = 0;

static void notify_watchdog(void *arg) {
    const uint32_t delay = (uint32_t)(uintptr_t)arg;
    hrt_sleep(delay);
    g_notify_watchdog = 1;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static hrt_config_t notify_cfg(void) {
    hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 3u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_SYSTICK
    };
    return cfg;
}

static void test_notify_invalid_targets(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = notify_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify invalid: init kernel");

    T_ASSERT_EQ_INT(-1, hrt_task_notify(-1, 1u, HRT_NOTIFY_OVERWRITE),
                    "negative task ID is rejected");
    T_ASSERT_EQ_INT(-1, hrt_task_notify(HARDRT_APP_MAX_TASKS, 1u, HRT_NOTIFY_OVERWRITE),
                    "private idle ID is rejected");
    T_ASSERT_EQ_INT(-1, hrt_task_notify(0, 1u, HRT_NOTIFY_OVERWRITE),
                    "unused application slot is rejected");
    T_ASSERT_EQ_INT(-1, hrt_task_notify_from_isr(0, 1u, HRT_NOTIFY_OVERWRITE, NULL),
                    "ISR producer rejects unused target");
    T_ASSERT_EQ_INT(-1, hrt_task_notify(0, 1u, (hrt_notify_action_t)99),
                    "unknown notification action is rejected");
}

static volatile uint32_t g_notify_before_value = 0u;
static volatile int g_notify_before_rc = -99;

static void notify_before_receiver(void *arg) {
    (void)arg;
    uint32_t value = 0u;
    g_notify_before_rc = hrt_task_notify_wait(0u, UINT32_MAX, &value);
    g_notify_before_value = value;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_notify_before_wait_accumulates_bits(void) {
    hrt__test_reset_scheduler_state();
    g_notify_before_value = 0u;
    g_notify_before_rc = -99;
    g_notify_watchdog = 0;

    hrt_config_t cfg = notify_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify-before-wait: init kernel");

    static uint32_t receiver_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    const int receiver = hrt_create_task(notify_before_receiver, NULL,
                                         receiver_stack, 1024, &high);
    hrt_create_task(notify_watchdog, (void *)(uintptr_t)200u,
                    watchdog_stack, 1024, &low);
    T_ASSERT_TRUE(receiver >= 0, "notify-before-wait: receiver created");
    T_ASSERT_EQ_INT(0, hrt_task_notify(receiver, 0x1u, HRT_NOTIFY_SET_BITS),
                    "first pre-wait set-bits succeeds");
    T_ASSERT_EQ_INT(0, hrt_task_notify(receiver, 0x4u, HRT_NOTIFY_SET_BITS),
                    "second pre-wait set-bits accumulates");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_notify_watchdog, "notify-before-wait: watchdog did not trip");
    T_ASSERT_EQ_INT(0, g_notify_before_rc, "pending notification makes wait immediate");
    T_ASSERT_EQ_UINT(0x5u, g_notify_before_value, "set-bits updates accumulate before wait");
}

static volatile uint32_t g_notify_no_overwrite_value = 0u;

static void notify_no_overwrite_receiver(void *arg) {
    (void)arg;
    uint32_t value = 0u;
    hrt_task_notify_wait(0u, UINT32_MAX, &value);
    g_notify_no_overwrite_value = value;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_notify_no_overwrite_preserves_pending(void) {
    hrt__test_reset_scheduler_state();
    g_notify_no_overwrite_value = 0u;
    g_notify_watchdog = 0;

    hrt_config_t cfg = notify_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify no-overwrite: init kernel");

    static uint32_t receiver_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    const int receiver = hrt_create_task(notify_no_overwrite_receiver, NULL,
                                         receiver_stack, 1024, &high);
    hrt_create_task(notify_watchdog, (void *)(uintptr_t)200u,
                    watchdog_stack, 1024, &low);

    T_ASSERT_EQ_INT(0, hrt_task_notify(receiver, 0x11u, HRT_NOTIFY_OVERWRITE),
                    "initial overwrite publishes pending value");
    T_ASSERT_EQ_INT(-1, hrt_task_notify(receiver, 0x22u, HRT_NOTIFY_NO_OVERWRITE),
                    "no-overwrite rejects replacement while pending");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_notify_watchdog, "notify no-overwrite: watchdog did not trip");
    T_ASSERT_EQ_UINT(0x11u, g_notify_no_overwrite_value,
                     "rejected no-overwrite preserves original notification");
}

static volatile int g_notify_isr_need = 0;
static volatile uint32_t g_notify_isr_value = 0u;
static int g_notify_isr_receiver_id = -1;

static void notify_isr_receiver(void *arg) {
    (void)arg;
    uint32_t value = 0u;
    if (hrt_task_notify_wait(0u, UINT32_MAX, &value) == 0) {
        g_notify_isr_value = value;
    }
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void notify_isr_sender(void *arg) {
    (void)arg;
    hrt_sleep(10u);
    int need = 0;
    hrt_task_notify_from_isr(g_notify_isr_receiver_id, 0x20u,
                             HRT_NOTIFY_SET_BITS, &need);
    g_notify_isr_need = need;
    hrt_yield();
    hrt_sleep(1000u);
}

static void test_notify_wait_before_isr_notify(void) {
    hrt__test_reset_scheduler_state();
    g_notify_isr_need = 0;
    g_notify_isr_value = 0u;
    g_notify_isr_receiver_id = -1;
    g_notify_watchdog = 0;

    hrt_config_t cfg = notify_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify ISR: init kernel");

    static uint32_t receiver_stack[1024], sender_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    g_notify_isr_receiver_id = hrt_create_task(notify_isr_receiver, NULL,
                                                receiver_stack, 1024, &high);
    hrt_create_task(notify_isr_sender, NULL, sender_stack, 1024, &low);
    hrt_create_task(notify_watchdog, (void *)(uintptr_t)200u,
                    watchdog_stack, 1024, &low);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_notify_watchdog, "notify ISR: watchdog did not trip");
    T_ASSERT_EQ_INT(1, g_notify_isr_need, "ISR notify reports higher-priority wake");
    T_ASSERT_EQ_UINT(0x20u, g_notify_isr_value, "waiter receives ISR notification value");
}

static hrt_sem_t g_notify_unrelated_sem;
static int g_notify_unrelated_receiver_id = -1;
static volatile int g_notify_unrelated_not_woken = 0;
static volatile int g_notify_unrelated_after_sem = 0;
static volatile uint32_t g_notify_unrelated_value = 0u;

static void notify_unrelated_receiver(void *arg) {
    (void)arg;
    hrt_sem_take(&g_notify_unrelated_sem);
    g_notify_unrelated_after_sem = 1;

    uint32_t value = 0u;
    hrt_task_notify_wait(0u, UINT32_MAX, &value);
    g_notify_unrelated_value = value;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void notify_unrelated_sender(void *arg) {
    (void)arg;
    hrt_sleep(10u);
    hrt_task_notify(g_notify_unrelated_receiver_id, 0x40u, HRT_NOTIFY_OVERWRITE);
    g_notify_unrelated_not_woken = (g_notify_unrelated_after_sem == 0) ? 1 : 0;
    hrt_sleep(5u);
    hrt_sem_give(&g_notify_unrelated_sem);
    hrt_sleep(1000u);
}

static void test_notify_does_not_wake_unrelated_ipc(void) {
    hrt__test_reset_scheduler_state();
    hrt_sem_init(&g_notify_unrelated_sem, 0u);
    g_notify_unrelated_receiver_id = -1;
    g_notify_unrelated_not_woken = 0;
    g_notify_unrelated_after_sem = 0;
    g_notify_unrelated_value = 0u;
    g_notify_watchdog = 0;

    hrt_config_t cfg = notify_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify unrelated IPC: init kernel");

    static uint32_t receiver_stack[1024], sender_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    g_notify_unrelated_receiver_id = hrt_create_task(notify_unrelated_receiver, NULL,
                                                      receiver_stack, 1024, &high);
    hrt_create_task(notify_unrelated_sender, NULL, sender_stack, 1024, &low);
    hrt_create_task(notify_watchdog, (void *)(uintptr_t)250u,
                    watchdog_stack, 1024, &low);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_notify_watchdog, "notify unrelated IPC: watchdog did not trip");
    T_ASSERT_EQ_INT(1, g_notify_unrelated_not_woken,
                    "notification does not wake task blocked on semaphore");
    T_ASSERT_EQ_INT(1, g_notify_unrelated_after_sem,
                    "receiver resumes only after semaphore give");
    T_ASSERT_EQ_UINT(0x40u, g_notify_unrelated_value,
                     "notification remains pending across unrelated IPC block");
}

static volatile uint32_t g_notify_take_first = 0u;
static volatile uint32_t g_notify_take_second = 0u;

static void notify_take_receiver(void *arg) {
    (void)arg;
    g_notify_take_first = hrt_task_notify_take(0);
    g_notify_take_second = hrt_task_notify_take(0);
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_notify_increment_and_take(void) {
    hrt__test_reset_scheduler_state();
    g_notify_take_first = 0u;
    g_notify_take_second = 0u;
    g_notify_watchdog = 0;

    hrt_config_t cfg = notify_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify take: init kernel");

    static uint32_t receiver_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    const int receiver = hrt_create_task(notify_take_receiver, NULL,
                                         receiver_stack, 1024, &high);
    hrt_create_task(notify_watchdog, (void *)(uintptr_t)200u,
                    watchdog_stack, 1024, &low);
    hrt_task_notify(receiver, 0u, HRT_NOTIFY_INCREMENT);
    hrt_task_notify(receiver, 0u, HRT_NOTIFY_INCREMENT);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_notify_watchdog, "notify take: watchdog did not trip");
    T_ASSERT_EQ_UINT(2u, g_notify_take_first, "first take observes accumulated count");
    T_ASSERT_EQ_UINT(1u, g_notify_take_second, "decrement-one take preserves remaining count");
}

static uint32_t g_notify_reuse_child_stack[1024];
static int g_notify_reuse_first_id = -1;
static volatile int g_notify_reuse_same_id = 0;
static volatile uint32_t g_notify_reuse_value = 0u;

static void notify_reuse_first_child(void *arg) {
    (void)arg;
    /* Return with a notification still pending. Slot reclamation must not leak
       that state into the next task. */
}

static void notify_reuse_second_child(void *arg) {
    (void)arg;
    uint32_t value = 0u;
    hrt_task_notify_wait(0u, UINT32_MAX, &value);
    g_notify_reuse_value = value;
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void notify_reuse_manager(void *arg) {
    (void)arg;
    hrt_sleep(5u);

    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    const int second_id = hrt_create_task(notify_reuse_second_child, NULL,
                                          g_notify_reuse_child_stack, 1024, &high);
    g_notify_reuse_same_id = (second_id == g_notify_reuse_first_id) ? 1 : 0;

    /* Runtime create does not force preemption. Yield so the new high-priority
       child can run and prove that stale pending state does not satisfy wait. */
    hrt_yield();
    hrt_task_notify(second_id, 0x2u, HRT_NOTIFY_OVERWRITE);
    hrt_sleep(1000u);
}

static void test_notify_state_resets_on_slot_reuse(void) {
    hrt__test_reset_scheduler_state();
    g_notify_reuse_first_id = -1;
    g_notify_reuse_same_id = 0;
    g_notify_reuse_value = 0u;
    g_notify_watchdog = 0;

    hrt_config_t cfg = notify_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify reuse: init kernel");

    static uint32_t manager_stack[1024], watchdog_stack[1024];
    hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 2u};
    hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 2u};
    g_notify_reuse_first_id = hrt_create_task(notify_reuse_first_child, NULL,
                                               g_notify_reuse_child_stack, 1024, &high);
    hrt_create_task(notify_reuse_manager, NULL, manager_stack, 1024, &low);
    hrt_create_task(notify_watchdog, (void *)(uintptr_t)250u,
                    watchdog_stack, 1024, &low);
    hrt_task_notify(g_notify_reuse_first_id, 0x80u, HRT_NOTIFY_OVERWRITE);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_notify_watchdog, "notify reuse: watchdog did not trip");
    T_ASSERT_EQ_INT(1, g_notify_reuse_same_id, "same exited slot is reclaimed for same stack");
    T_ASSERT_EQ_UINT(0x2u, g_notify_reuse_value,
                     "reused task sees only new notification, not stale pending state");
}

static const test_case_t CASES[] = {
    {"Notify: invalid targets and action", test_notify_invalid_targets},
    {"Notify: notify-before-wait accumulates bits", test_notify_before_wait_accumulates_bits},
    {"Notify: no-overwrite preserves pending value", test_notify_no_overwrite_preserves_pending},
    {"Notify: wait-before-ISR-notify need_switch", test_notify_wait_before_isr_notify},
    {"Notify: unrelated IPC block is not woken", test_notify_does_not_wake_unrelated_ipc},
    {"Notify: increment and counting take", test_notify_increment_and_take},
    {"Notify: state reset on exited-slot reuse", test_notify_state_resets_on_slot_reuse}
};

const test_case_t *get_tests_notify(int *out_count) {
    if (out_count != NULL) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
