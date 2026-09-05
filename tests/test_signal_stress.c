/* Deterministic stress and invariant coverage for v0.5 event/notification signaling. */
#include "test_common.h"
#include "hardrt_event.h"
#include "hardrt_notify.h"
#include "hardrt_sem.h"
#include "hardrt_kernel.h"

#include <limits.h>
#include <stddef.h>

#define SIGNAL_STACK_WORDS 1024
#define SIGNAL_STRESS_ITERS 1024u
#define SIGNAL_STRESS_SEED 0x00C0FFEEu

#ifdef HARDRT_TEST_HOOKS
int hrt__test_event_register_waiter(hrt_event_t *event,
                                    int task_id,
                                    hrt_event_bits_t mask,
                                    unsigned options);
#endif

static int signal_invariants_ok(const hrt_event_t *event) {
#ifdef HARDRT_TEST_HOOKS
    uint8_t seen[HARDRT_APP_MAX_TASKS];
    memset(seen, 0, sizeof(seen));

    if (event != NULL) {
        if (event->wait_count > HARDRT_APP_MAX_TASKS) return 0;
        for (uint8_t i = 0u; i < event->wait_count; ++i) {
            const uint8_t id = event->wait_q[i];
            if (id >= HARDRT_APP_MAX_TASKS) return 0;
            if (seen[id] != 0u || event->wait_active[id] == 0u) return 0;
            seen[id] = 1u;
        }
    }

    for (int id = 0; id < HARDRT_APP_MAX_TASKS; ++id) {
        const int slot = hrt__test_slot_state(id);
        const int occurrences = hrt__test_ready_occurrences(id);

        if (slot == HRT_SLOT_UNUSED) {
            if (occurrences != 0) return 0;
            if (event != NULL && event->wait_active[id] != 0u) return 0;
            continue;
        }
        if (slot != HRT_SLOT_USED) return 0;

        const int state = hrt__test_task_state(id);
        if (state == HRT_READY) {
            if (occurrences != 1) return 0;
        } else if (occurrences != 0) {
            return 0;
        }

        const _hrt_tcb_t *task = hrt__tcb(id);
        if (task == NULL) return 0;
        if (task->notify_waiting != 0u && state != HRT_BLOCKED) return 0;

        if (event != NULL) {
            if ((event->wait_active[id] != 0u) != (seen[id] != 0u)) return 0;
            if (seen[id] != 0u && state != HRT_BLOCKED) return 0;
        }
    }
#else
    (void)event;
#endif
    return 1;
}

static hrt_config_t signal_cfg(const hrt_policy_t policy) {
    hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = policy,
        .default_slice = 0u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_EXTERNAL
    };
    return cfg;
}

static void test_signal_storage_and_event_waiter_capacity(void) {
    hrt__test_reset_scheduler_state();
    const hrt_config_t cfg = signal_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "signal storage: init kernel");

    hrt_event_t event;
    hrt_event_init(&event);

    printf("SIGNAL_STORAGE app_tasks=%u event_bytes=%u tcb_bytes=%u\n",
           (unsigned)HARDRT_APP_MAX_TASKS,
           (unsigned)sizeof(hrt_event_t),
           (unsigned)sizeof(_hrt_tcb_t));

#if HARDRT_APP_MAX_TASKS == 8
    T_ASSERT_EQ_UINT(96u, (unsigned)sizeof(hrt_event_t),
                     "default eight-task event object is 96 bytes");
#endif
#if UINTPTR_MAX == UINT32_MAX
    T_ASSERT_EQ_UINT(40u, (unsigned)sizeof(_hrt_tcb_t),
                     "32-bit private TCB is 40 bytes with notification state");
#endif

#ifdef HARDRT_TEST_HOOKS
    for (int id = 0; id < HARDRT_APP_MAX_TASKS; ++id) {
        const hrt_event_bits_t mask = (hrt_event_bits_t)(1u << ((unsigned)id & 31u));
        T_ASSERT_EQ_INT(0,
                        hrt__test_event_register_waiter(&event, id, mask,
                                                        HRT_EVENT_WAIT_ANY),
                        "event capacity: register waiter");
    }
    T_ASSERT_EQ_UINT((unsigned)HARDRT_APP_MAX_TASKS, event.wait_count,
                     "event capacity accepts every configured application waiter slot");
    T_ASSERT_EQ_INT(-1,
                    hrt__test_event_register_waiter(&event, 0, 1u,
                                                    HRT_EVENT_WAIT_ANY),
                    "event capacity rejects duplicate registration when full");
    T_ASSERT_EQ_INT(-1,
                    hrt__test_event_register_waiter(&event, HARDRT_APP_MAX_TASKS,
                                                    1u, HRT_EVENT_WAIT_ANY),
                    "event capacity rejects out-of-range waiter ID");
#else
    printf("SKIP: exact event waiter-capacity registration requires HARDRT_TEST_HOOKS.\n");
#endif
}

static int g_state_receiver_id = -1;
static volatile int g_state_running_seen = 0;
static volatile int g_state_exited_seen = 0;
static volatile int g_state_exited_notify_rc = 0;
static volatile uint32_t g_state_first = 0u;
static volatile uint32_t g_state_second = 0u;
static volatile uint32_t g_state_burst_first = 0u;
static volatile uint32_t g_state_burst_second = 0u;
static volatile uint32_t g_state_saturated = 0u;
static uint32_t g_state_receiver_stack[SIGNAL_STACK_WORDS];
static uint32_t g_state_manager_stack[SIGNAL_STACK_WORDS];

static void signal_state_receiver(void *arg) {
    (void)arg;
#ifdef HARDRT_TEST_HOOKS
    g_state_running_seen =
        (hrt__test_task_state(g_state_receiver_id) == HRT_RUNNING) ? 1 : 0;
#endif

    uint32_t value = 0u;
    if (hrt_task_notify_wait(0x3u, 0x4u, &value) != 0) return;
    g_state_first = value;

    if (hrt_task_notify(g_state_receiver_id, 0x1u, HRT_NOTIFY_SET_BITS) != 0) return;
    if (hrt_task_notify_wait(0u, UINT32_MAX, &value) != 0) return;
    g_state_second = value;

    for (unsigned i = 0u; i < 64u; ++i) {
        if (hrt_task_notify(g_state_receiver_id, 0u, HRT_NOTIFY_INCREMENT) != 0) return;
    }
    g_state_burst_first = hrt_task_notify_take(0);
    g_state_burst_second = hrt_task_notify_take(1);

    if (hrt_task_notify(g_state_receiver_id, UINT32_MAX,
                        HRT_NOTIFY_OVERWRITE) != 0) return;
    if (hrt_task_notify(g_state_receiver_id, 0u, HRT_NOTIFY_INCREMENT) != 0) return;
    g_state_saturated = hrt_task_notify_take(1);
}

static void signal_state_manager(void *arg) {
    (void)arg;
#ifdef HARDRT_TEST_HOOKS
    g_state_exited_seen =
        (hrt__test_task_state(g_state_receiver_id) == HRT_EXITED) ? 1 : 0;
#endif
    g_state_exited_notify_rc =
        hrt_task_notify(g_state_receiver_id, 0x55u, HRT_NOTIFY_OVERWRITE);
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_notify_state_clear_burst_and_exit_matrix(void) {
    hrt__test_reset_scheduler_state();
    g_state_receiver_id = -1;
    g_state_running_seen = 0;
    g_state_exited_seen = 0;
    g_state_exited_notify_rc = 0;
    g_state_first = 0u;
    g_state_second = 0u;
    g_state_burst_first = 0u;
    g_state_burst_second = 0u;
    g_state_saturated = 0u;

    const hrt_config_t cfg = signal_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "notify state matrix: init kernel");

    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 0u};
    const hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 0u};
    g_state_receiver_id =
        hrt_create_task(signal_state_receiver, NULL, g_state_receiver_stack,
                        SIGNAL_STACK_WORDS, &high);
    T_ASSERT_TRUE(g_state_receiver_id >= 0, "notify state matrix: receiver created");
#ifdef HARDRT_TEST_HOOKS
    T_ASSERT_EQ_INT(HRT_READY, hrt__test_task_state(g_state_receiver_id),
                    "notification target starts READY");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(g_state_receiver_id),
                    "READY target has exactly one ready-queue membership");
#endif
    T_ASSERT_EQ_INT(0,
                    hrt_task_notify(g_state_receiver_id, 0xFu,
                                    HRT_NOTIFY_OVERWRITE),
                    "notification update succeeds while target is READY");
#ifdef HARDRT_TEST_HOOKS
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(g_state_receiver_id),
                    "READY notification does not duplicate ready-queue membership");
#endif
    T_ASSERT_TRUE(hrt_create_task(signal_state_manager, NULL, g_state_manager_stack,
                                  SIGNAL_STACK_WORDS, &low) >= 0,
                  "notify state matrix: manager created");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_state_running_seen,
                    "notification target is observed RUNNING inside its task");
    T_ASSERT_EQ_UINT(0xCu, g_state_first,
                     "clear-on-entry applies before returning pending notification");
    T_ASSERT_EQ_UINT(0x9u, g_state_second,
                     "clear-on-exit residual combines with the next set-bits update");
    T_ASSERT_EQ_UINT(64u, g_state_burst_first,
                     "64-increment burst is preserved by counting notification");
    T_ASSERT_EQ_UINT(63u, g_state_burst_second,
                     "decrement-one take preserves the residual burst count");
    T_ASSERT_EQ_UINT(UINT32_MAX, g_state_saturated,
                     "notification increment saturates at UINT32_MAX");
    T_ASSERT_EQ_INT(1, g_state_exited_seen,
                    "returned task reaches EXITED before manager notification");
    T_ASSERT_EQ_INT(-1, g_state_exited_notify_rc,
                    "notification rejects EXITED target");
}

static hrt_event_t g_sim_event;
static hrt_sem_t g_sim_armed;
static hrt_sem_t g_sim_done;
static int g_sim_event_id = -1;
static int g_sim_notify_id = -1;
static volatile int g_sim_event_need = 0;
static volatile int g_sim_notify_need = 0;
static volatile int g_sim_invariants = 0;
static volatile int g_sim_sequence[2];
static volatile int g_sim_sequence_count = 0;
static uint32_t g_sim_event_stack[SIGNAL_STACK_WORDS];
static uint32_t g_sim_notify_stack[SIGNAL_STACK_WORDS];
static uint32_t g_sim_controller_stack[SIGNAL_STACK_WORDS];

static void signal_sim_event_waiter(void *arg) {
    (void)arg;
    (void)hrt_sem_give(&g_sim_armed);
    hrt_event_bits_t matched = 0u;
    if (hrt_event_wait(&g_sim_event, 0x1u, HRT_EVENT_CLEAR_ON_EXIT,
                       &matched) == 0 && matched == 0x1u) {
        const int slot = g_sim_sequence_count++;
        if (slot < 2) g_sim_sequence[slot] = 1;
    }
    (void)hrt_sem_give(&g_sim_done);
}

static void signal_sim_notify_waiter(void *arg) {
    (void)arg;
    (void)hrt_sem_give(&g_sim_armed);
    uint32_t value = 0u;
    if (hrt_task_notify_wait(0u, UINT32_MAX, &value) == 0 && value == 0x20u) {
        const int slot = g_sim_sequence_count++;
        if (slot < 2) g_sim_sequence[slot] = 2;
    }
    (void)hrt_sem_give(&g_sim_done);
}

static void signal_sim_controller(void *arg) {
    (void)arg;
    (void)hrt_sem_take(&g_sim_armed);
    (void)hrt_sem_take(&g_sim_armed);

    int event_need = 0;
    int notify_need = 0;
    (void)hrt_event_set_from_isr(&g_sim_event, 0x1u, &event_need);
    (void)hrt_task_notify_from_isr(g_sim_notify_id, 0x20u,
                                   HRT_NOTIFY_OVERWRITE, &notify_need);
    g_sim_event_need = event_need;
    g_sim_notify_need = notify_need;

#ifdef HARDRT_TEST_HOOKS
    g_sim_invariants = signal_invariants_ok(&g_sim_event) &&
                       hrt__test_ready_occurrences(g_sim_event_id) == 1 &&
                       hrt__test_ready_occurrences(g_sim_notify_id) == 1;
#else
    g_sim_invariants = signal_invariants_ok(&g_sim_event);
#endif

    hrt_yield();
    (void)hrt_sem_take(&g_sim_done);
    (void)hrt_sem_take(&g_sim_done);
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_simultaneous_event_notification_wake(void) {
    hrt__test_reset_scheduler_state();
    hrt_event_init(&g_sim_event);
    hrt_sem_init_counting(&g_sim_armed, 0u, 2u);
    hrt_sem_init_counting(&g_sim_done, 0u, 2u);
    g_sim_event_id = -1;
    g_sim_notify_id = -1;
    g_sim_event_need = 0;
    g_sim_notify_need = 0;
    g_sim_invariants = 0;
    g_sim_sequence_count = 0;
    g_sim_sequence[0] = 0;
    g_sim_sequence[1] = 0;

    const hrt_config_t cfg = signal_cfg(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "simultaneous signals: init kernel");
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 0u};
    const hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 0u};

    g_sim_event_id =
        hrt_create_task(signal_sim_event_waiter, NULL, g_sim_event_stack,
                        SIGNAL_STACK_WORDS, &high);
    g_sim_notify_id =
        hrt_create_task(signal_sim_notify_waiter, NULL, g_sim_notify_stack,
                        SIGNAL_STACK_WORDS, &high);
    T_ASSERT_TRUE(g_sim_event_id >= 0 && g_sim_notify_id >= 0,
                  "simultaneous signals: waiters created");
    T_ASSERT_TRUE(hrt_create_task(signal_sim_controller, NULL,
                                  g_sim_controller_stack, SIGNAL_STACK_WORDS,
                                  &low) >= 0,
                  "simultaneous signals: controller created");

    hrt_start();

    T_ASSERT_EQ_INT(1, g_sim_event_need,
                    "event ISR producer requests priority handoff");
    T_ASSERT_EQ_INT(1, g_sim_notify_need,
                    "notification ISR producer requests priority handoff");
    T_ASSERT_EQ_INT(1, g_sim_invariants,
                    "simultaneous wakes preserve waiter and ready-queue invariants");
    T_ASSERT_EQ_INT(2, g_sim_sequence_count,
                    "both simultaneous signal waiters complete");
    T_ASSERT_EQ_INT(1, g_sim_sequence[0],
                    "same-priority event wake runs first after first publication");
    T_ASSERT_EQ_INT(2, g_sim_sequence[1],
                    "same-priority notification wake follows FIFO publication order");
}

static hrt_event_t g_stress_event;
static hrt_sem_t g_stress_armed;
static hrt_sem_t g_stress_done;
static uint32_t g_stress_pattern[SIGNAL_STRESS_ITERS];
static int g_stress_worker_id = -1;
static hrt_policy_t g_stress_policy = HRT_SCHED_PRIORITY_RR;
static volatile unsigned g_stress_completed = 0u;
static volatile int g_stress_error = 0;
static volatile unsigned g_stress_fail_iteration = UINT_MAX;
static volatile uint32_t g_stress_ticks = 0u;
static uint32_t g_stress_worker_stack[SIGNAL_STACK_WORDS];
static uint32_t g_stress_controller_stack[SIGNAL_STACK_WORDS];

static void prepare_stress_pattern(void) {
    uint32_t state = SIGNAL_STRESS_SEED;
    for (unsigned i = 0u; i < SIGNAL_STRESS_ITERS; ++i) {
        state = state * 1664525u + 1013904223u;
        g_stress_pattern[i] = state;
    }
}

static hrt_event_bits_t stress_event_mask(const uint32_t r, int *wait_all) {
    const unsigned bit = (r >> 4u) & 3u;
    const hrt_event_bits_t first = (hrt_event_bits_t)(1u << bit);
    *wait_all = ((r >> 2u) & 1u) != 0u ? 1 : 0;
    if (*wait_all == 0) return first;
    const unsigned second_bit = (bit + 1u) & 3u;
    return first | (hrt_event_bits_t)(1u << second_bit);
}

static hrt_notify_action_t stress_notify_action(const uint32_t r) {
    switch ((r >> 5u) & 3u) {
        case 0u: return HRT_NOTIFY_SET_BITS;
        case 1u: return HRT_NOTIFY_OVERWRITE;
        case 2u: return HRT_NOTIFY_NO_OVERWRITE;
        default: return HRT_NOTIFY_INCREMENT;
    }
}

static uint32_t stress_notify_value(const uint32_t r) {
    return ((r >> 8u) & 0x7FFFu) | 1u;
}

static void stress_set_failure(const int code, const unsigned iteration) {
    if (g_stress_error == 0) {
        g_stress_error = code;
        g_stress_fail_iteration = iteration;
    }
}

static void stress_worker_failure(const int code, const unsigned iteration) {
    stress_set_failure(code, iteration);
    /* Always release the controller. A detected failure must become evidence,
       not a deadlocked test that merely occupies a CI runner until timeout. */
    (void)hrt_sem_give(&g_stress_done);
}

static void signal_stress_worker(void *arg) {
    (void)arg;
    for (unsigned i = 0u; i < SIGNAL_STRESS_ITERS; ++i) {
        const uint32_t r = g_stress_pattern[i];
        (void)hrt_sem_give(&g_stress_armed);

        if ((r & 1u) == 0u) {
            int wait_all = 0;
            const hrt_event_bits_t mask = stress_event_mask(r, &wait_all);
            unsigned options = wait_all != 0 ? (unsigned)HRT_EVENT_WAIT_ALL
                                             : (unsigned)HRT_EVENT_WAIT_ANY;
            if (((r >> 3u) & 1u) != 0u) options |= (unsigned)HRT_EVENT_CLEAR_ON_EXIT;

            hrt_event_bits_t matched = 0u;
            if (hrt_event_wait(&g_stress_event, mask, options, &matched) != 0 ||
                matched != mask) {
                stress_worker_failure(1000, i);
                return;
            }
            if ((options & (unsigned)HRT_EVENT_CLEAR_ON_EXIT) == 0u &&
                hrt_event_clear(&g_stress_event, mask) != 0) {
                stress_worker_failure(2000, i);
                return;
            }
        } else {
            uint32_t value = 0u;
            if (hrt_task_notify_wait(0u, UINT32_MAX, &value) != 0) {
                stress_worker_failure(3000, i);
                return;
            }
            const hrt_notify_action_t action = stress_notify_action(r);
            const uint32_t expected = action == HRT_NOTIFY_INCREMENT
                                          ? 1u
                                          : stress_notify_value(r);
            if (value != expected) {
                stress_worker_failure(4000, i);
                return;
            }
        }

        g_stress_completed = i + 1u;
        (void)hrt_sem_give(&g_stress_done);
    }
}

static void signal_stress_controller(void *arg) {
    (void)arg;
    const int expect_need = g_stress_policy == HRT_SCHED_RR ? 0 : 1;

    for (unsigned i = 0u; i < SIGNAL_STRESS_ITERS; ++i) {
        if (hrt_sem_take(&g_stress_armed) != 0) {
            stress_set_failure(5000, i);
            break;
        }
#ifdef HARDRT_TEST_HOOKS
        if (hrt__test_task_state(g_stress_worker_id) != HRT_BLOCKED ||
            !signal_invariants_ok(&g_stress_event)) {
            stress_set_failure(6000, i);
            break;
        }
#endif

        hrt_tick_from_isr();
        g_stress_ticks++;

        const uint32_t r = g_stress_pattern[i];
        int need = -1;
        int rc = 0;

        if ((r & 1u) == 0u) {
            int wait_all = 0;
            const hrt_event_bits_t mask = stress_event_mask(r, &wait_all);
            if (wait_all != 0) {
                const hrt_event_bits_t first = mask & (hrt_event_bits_t)(0u - mask);
                int first_need = -1;
                rc = hrt_event_set_from_isr(&g_stress_event, first, &first_need);
                if (rc != 0 || first_need != 0) {
                    stress_set_failure(7000, i);
                    break;
                }
#ifdef HARDRT_TEST_HOOKS
                if (hrt__test_task_state(g_stress_worker_id) != HRT_BLOCKED ||
                    !signal_invariants_ok(&g_stress_event)) {
                    stress_set_failure(8000, i);
                    break;
                }
#endif
                hrt_tick_from_isr();
                g_stress_ticks++;
                rc = hrt_event_set_from_isr(&g_stress_event, mask & ~first, &need);
            } else {
                rc = hrt_event_set_from_isr(&g_stress_event, mask, &need);
            }
        } else {
            const hrt_notify_action_t action = stress_notify_action(r);
            rc = hrt_task_notify_from_isr(g_stress_worker_id,
                                          stress_notify_value(r), action, &need);
        }

        if (rc != 0 || need != expect_need) {
            stress_set_failure(9000, i);
            break;
        }
#ifdef HARDRT_TEST_HOOKS
        if (hrt__test_ready_occurrences(g_stress_worker_id) != 1 ||
            !signal_invariants_ok(&g_stress_event)) {
            stress_set_failure(10000, i);
            break;
        }
#endif

        hrt_yield();
        if (hrt_sem_take(&g_stress_done) != 0) {
            stress_set_failure(11000, i);
            break;
        }
        if (g_stress_error != 0) break;
        if (g_stress_completed != i + 1u) {
            stress_set_failure(12000, i);
            break;
        }
#ifdef HARDRT_TEST_HOOKS
        if (!signal_invariants_ok(&g_stress_event)) {
            stress_set_failure(13000, i);
            break;
        }
#endif
    }

    hrt__test_stop_scheduler();
    hrt_yield();
}

static void print_stress_failure_context(const hrt_policy_t policy) {
    if (g_stress_error == 0 || g_stress_fail_iteration >= SIGNAL_STRESS_ITERS) return;

    const unsigned i = g_stress_fail_iteration;
    const uint32_t r = g_stress_pattern[i];
    if ((r & 1u) == 0u) {
        int wait_all = 0;
        const hrt_event_bits_t mask = stress_event_mask(r, &wait_all);
        printf("SIGNAL_STRESS_FAILURE primitive=event policy=%d task=%d iteration=%u "
               "pattern=0x%08x mask=0x%08x wait_all=%d clear_on_exit=%u error=%d\n",
               (int)policy, g_stress_worker_id, i, (unsigned)r, (unsigned)mask,
               wait_all, (unsigned)((r >> 3u) & 1u), g_stress_error);
    } else {
        const hrt_notify_action_t action = stress_notify_action(r);
        const uint32_t value = stress_notify_value(r);
        printf("SIGNAL_STRESS_FAILURE primitive=notification policy=%d task=%d iteration=%u "
               "pattern=0x%08x value=0x%08x action=%d error=%d\n",
               (int)policy, g_stress_worker_id, i, (unsigned)r, (unsigned)value,
               (int)action, g_stress_error);
    }
}

static void run_signal_stress(const hrt_policy_t policy, const char *label) {
    hrt__test_reset_scheduler_state();
    hrt_event_init(&g_stress_event);
    hrt_sem_init(&g_stress_armed, 0u);
    hrt_sem_init(&g_stress_done, 0u);
    prepare_stress_pattern();
    g_stress_worker_id = -1;
    g_stress_policy = policy;
    g_stress_completed = 0u;
    g_stress_error = 0;
    g_stress_fail_iteration = UINT_MAX;
    g_stress_ticks = 0u;

    const hrt_config_t cfg = signal_cfg(policy);
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), label);
    const hrt_task_attr_t high = {.priority = HRT_PRIO0, .timeslice = 0u};
    const hrt_task_attr_t low = {.priority = HRT_PRIO2, .timeslice = 0u};

    g_stress_worker_id =
        hrt_create_task(signal_stress_worker, NULL, g_stress_worker_stack,
                        SIGNAL_STACK_WORDS, &high);
    T_ASSERT_TRUE(g_stress_worker_id >= 0, "signal stress: worker created");
    T_ASSERT_TRUE(hrt_create_task(signal_stress_controller, NULL,
                                  g_stress_controller_stack, SIGNAL_STACK_WORDS,
                                  &low) >= 0,
                  "signal stress: controller created");

    hrt_start();

    printf("SIGNAL_STRESS policy=%d seed=0x%08x iterations=%u completed=%u ticks=%u error=%d\n",
           (int)policy, (unsigned)SIGNAL_STRESS_SEED,
           (unsigned)SIGNAL_STRESS_ITERS, (unsigned)g_stress_completed,
           (unsigned)g_stress_ticks, g_stress_error);
    print_stress_failure_context(policy);

    T_ASSERT_EQ_INT(0, g_stress_error,
                    "signal stress completes without state/invariant failure");
    T_ASSERT_EQ_UINT(SIGNAL_STRESS_ITERS, g_stress_completed,
                     "signal stress completes every deterministic operation");
    T_ASSERT_TRUE(g_stress_ticks >= SIGNAL_STRESS_ITERS,
                  "external tick activity is interleaved with every synchronization cycle");
    T_ASSERT_TRUE(signal_invariants_ok(&g_stress_event),
                  "signal stress leaves scheduler/waiter invariants valid");
}

static void test_signal_stress_priority(void) {
    run_signal_stress(HRT_SCHED_PRIORITY,
                      "signal stress priority: init kernel");
}

static void test_signal_stress_global_rr(void) {
    run_signal_stress(HRT_SCHED_RR,
                      "signal stress global RR: init kernel");
}

static void test_signal_stress_priority_rr(void) {
    run_signal_stress(HRT_SCHED_PRIORITY_RR,
                      "signal stress priority RR: init kernel");
}

static const test_case_t CASES[] = {
    {"Signals: storage and maximum event waiter capacity",
     test_signal_storage_and_event_waiter_capacity},
    {"Signals: notification state/clear/burst/exit matrix",
     test_notify_state_clear_burst_and_exit_matrix},
    {"Signals: simultaneous event and notification wake",
     test_simultaneous_event_notification_wake},
    {"Signals: deterministic stress under priority",
     test_signal_stress_priority},
    {"Signals: deterministic stress under global RR",
     test_signal_stress_global_rr},
    {"Signals: deterministic stress under priority RR",
     test_signal_stress_priority_rr},
};

const test_case_t *get_tests_signal_stress(int *out_count) {
    if (out_count != NULL) {
        *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    }
    return CASES;
}
