/* SPDX-License-Identifier: Apache-2.0 */
#include "test_common.h"
#include "hardrt_mutex.h"
#include "hardrt_port_int.h"

#define STACK_WORDS 1024u
static uint32_t stack_a[STACK_WORDS];
static uint32_t stack_b[STACK_WORDS];
static uint32_t stack_c[STACK_WORDS];

static hrt_mutex_t lock_a;
static hrt_mutex_t lock_b;
static volatile int low_id, mid_id, high_id;
static volatile int saw_boost, saw_restore, medium_before_unlock;
static volatile int middle_runs, result, high_result, got_handoff;
static volatile int chain_seen, cycle_rejected;
static volatile int owner_timeout_prio, timed_out_waiter;
static volatile int base_before, eff_before;
static volatile int low_quantum_before, low_quantum_after;
static volatile int state_stage;

static void stop_test(void) {
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void reset_case(hrt_policy_t policy) {
    hrt__test_reset_scheduler_state();
    hrt_mutex_init(&lock_a);
    hrt_mutex_init(&lock_b);
    low_id = mid_id = high_id = -1;
    saw_boost = saw_restore = medium_before_unlock = 0;
    middle_runs = result = high_result = got_handoff = 0;
    chain_seen = cycle_rejected = 0;
    owner_timeout_prio = timed_out_waiter = 0;
    low_quantum_before = low_quantum_after = -1;
    state_stage = 0;
    hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = policy,
        .default_slice = 5u,
        .tick_src = HRT_TICK_EXTERNAL
    };
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg), "initialize PI fixture");
}

static int make_task(hrt_task_fn fn, uint32_t *stack, hrt_prio_t priority) {
    hrt_task_attr_t attr = { .priority = priority, .timeslice = 5u };
    return hrt_create_task(fn, NULL, stack, STACK_WORDS, &attr);
}

static void competitor(void *arg) {
    (void)arg;
    middle_runs++;
    hrt_yield();
}

static void high_waiter(void *arg) {
    (void)arg;
    high_result = hrt_mutex_lock(&lock_a);
    if (high_result == 0) {
        saw_restore = (hrt__test_task_effective_priority(low_id) == HRT_PRIO2);
        got_handoff = 1;
        T_ASSERT_EQ_INT(high_id, lock_a.owner,
                        "direct mutex handoff gives the high waiter ownership");
        T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_a),
                        "high waiter releases direct handoff");
    }
    stop_test();
}

static void inversion_owner(void *arg) {
    (void)arg;
    low_id = hrt__get_current();
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "low task locks first");
    T_ASSERT_EQ_UINT(HRT_PRIO2, hrt__test_task_base_priority(low_id),
                     "owner base priority remains its creation priority");

    high_id = make_task(high_waiter, stack_b, HRT_PRIO0);
    mid_id = make_task(competitor, stack_c, HRT_PRIO1);
    T_ASSERT_TRUE(high_id >= 0 && mid_id >= 0, "created high waiter and medium competitor");
    hrt_yield();
    base_before = hrt__test_task_base_priority(low_id);
    eff_before = hrt__test_task_effective_priority(low_id);
    saw_boost = (eff_before == HRT_PRIO0);
    medium_before_unlock = middle_runs;

    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_a), "boosted owner unlocks");
    saw_restore = hrt__test_task_effective_priority(low_id) == HRT_PRIO2;
}

static void run_inversion(hrt_policy_t policy, int expect_medium) {
    reset_case(policy);
    low_id = make_task(inversion_owner, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(low_id >= 0, "created low owner");
    hrt_start();
    T_ASSERT_EQ_INT(1, saw_boost, "contended owner inherited high priority");
    T_ASSERT_EQ_INT(HRT_PRIO2, base_before, "immutable base priority preserved");
    T_ASSERT_EQ_INT(1, saw_restore, "unlock withdrew donation");
    T_ASSERT_EQ_INT(1, got_handoff, "high waiter received ownership");
    T_ASSERT_EQ_INT(0, high_result, "high mutex lock returned success");
    if (expect_medium == 0) {
        T_ASSERT_EQ_INT(0, medium_before_unlock,
                        "medium task cannot run ahead of boosted owner");
    } else {
        T_ASSERT_TRUE(medium_before_unlock > 0,
                      "global RR ignores inherited priorities as documented");
    }
}
static void test_fixed_priority_inversion(void) {
    run_inversion(HRT_SCHED_PRIORITY, 0);
}
static void test_priority_rr_inversion(void) {
    run_inversion(HRT_SCHED_PRIORITY_RR, 0);
}
static void test_global_rr_pi_is_metadata_only(void) {
    run_inversion(HRT_SCHED_RR, 1);
}

/* Nested acquisition and transitive donation:
 * B owns B -> A owns A and blocks on B -> HIGH blocks on A.
 * Propagation must boost B across both mutexes while it is READY.
 */
static void chain_high(void *arg) {
    (void)arg;
    high_result = hrt_mutex_lock(&lock_a);
    if (high_result == 0) {
        got_handoff = 1;
        (void)hrt_mutex_unlock(&lock_a);
    }
    stop_test();
}
static void chain_middle(void *arg) {
    (void)arg;
    mid_id = hrt__get_current();
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "middle owns first lock");
    high_id = make_task(chain_high, stack_c, HRT_PRIO0);
    T_ASSERT_TRUE(high_id >= 0, "created high chain waiter");
    hrt_yield();
    T_ASSERT_EQ_INT(HRT_PRIO0, hrt__test_task_effective_priority(mid_id),
                    "middle inherited high priority");
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_b), "middle blocks then owns second lock");
    (void)hrt_mutex_unlock(&lock_b);
    (void)hrt_mutex_unlock(&lock_a);
}
static void chain_low(void *arg) {
    (void)arg;
    low_id = hrt__get_current();
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_b), "low holds second lock");
    mid_id = make_task(chain_middle, stack_b, HRT_PRIO1);
    T_ASSERT_TRUE(mid_id >= 0, "created middle chain owner");
    hrt_yield();
    chain_seen = hrt__test_task_effective_priority(low_id) == HRT_PRIO0;
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_b), "low releases chained lock");
}
static void test_transitive_chain(void) {
    reset_case(HRT_SCHED_PRIORITY);
    low_id = make_task(chain_low, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(low_id >= 0, "created low chain owner");
    hrt_start();
    T_ASSERT_EQ_INT(1, chain_seen, "priority donation crossed two mutex owners");
    T_ASSERT_EQ_INT(1, got_handoff, "high waiter completed transitive handoff");
}

static void cycle_second(void *arg) {
    (void)arg;
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_b), "second owns lock B");
    result = hrt_mutex_lock(&lock_a);
    if (result == 0) (void)hrt_mutex_unlock(&lock_a);
    (void)hrt_mutex_unlock(&lock_b);
    stop_test();
}
static void cycle_first(void *arg) {
    (void)arg;
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "first owns lock A");
    mid_id = make_task(cycle_second, stack_b, HRT_PRIO0);
    T_ASSERT_TRUE(mid_id >= 0, "created cycle competitor");
    hrt_yield();
    cycle_rejected = hrt_mutex_lock(&lock_b) == -1;
    (void)hrt_mutex_unlock(&lock_a);
}
static void test_wait_for_cycle_rejected(void) {
    reset_case(HRT_SCHED_PRIORITY);
    low_id = make_task(cycle_first, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(low_id >= 0, "created first cycle owner");
    hrt_start();
    T_ASSERT_EQ_INT(1, cycle_rejected, "mutex wait-for cycle rejected before blocking");
    T_ASSERT_EQ_INT(0, result, "valid first waiter completed after cycle rejection");
}

static void timeout_waiter(void *arg) {
    (void)arg;
    const hrt_tick_t deadline = hrt_tick_now() + 3u;
    timed_out_waiter = hrt_mutex_lock_until(&lock_a, deadline);
    stop_test();
}
static void timeout_owner(void *arg) {
    (void)arg;
    low_id = hrt__get_current();
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "owner locks for timeout");
    high_id = make_task(timeout_waiter, stack_b, HRT_PRIO0);
    T_ASSERT_TRUE(high_id >= 0, "created timeout waiter");
    hrt_yield();
    T_ASSERT_EQ_INT(HRT_PRIO0, hrt__test_task_effective_priority(low_id),
                    "timeout waiter donated priority");
    for (int i = 0; i < 3; ++i) hrt_tick_from_isr();
    owner_timeout_prio = hrt__test_task_effective_priority(low_id);
    state_stage = lock_a.count_wait;
    (void)hrt_mutex_unlock(&lock_a);
}
static void test_timeout_withdraws_donation(void) {
    reset_case(HRT_SCHED_PRIORITY);
    low_id = make_task(timeout_owner, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(low_id >= 0, "created timed mutex owner");
    hrt_start();
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, timed_out_waiter,
                    "timed waiter expired without receiving ownership");
    T_ASSERT_EQ_INT(HRT_PRIO2, owner_timeout_prio,
                    "expiry withdrew donation before READY publication");
    T_ASSERT_EQ_INT(0, state_stage, "timed-out waiter is unlinked");
}

/* With two mutex waiters, expiry of the highest donor must restore the
 * owner only to the next-highest remaining waiter, not its base priority. */
static volatile int multi_wait_high_result;
static volatile int multi_wait_mid_acquired;
static volatile int multi_wait_prio_after_expiry;
static volatile int multi_wait_remaining;
static void multi_wait_high(void *arg) {
    (void)arg;
    multi_wait_high_result = hrt_mutex_lock_until(&lock_a, hrt_tick_now() + 3u);
}
static void multi_wait_middle(void *arg) {
    (void)arg;
    const int rc = hrt_mutex_lock(&lock_a);
    multi_wait_mid_acquired = (rc == 0);
    if (rc == 0) (void)hrt_mutex_unlock(&lock_a);
    stop_test();
}
static void multi_wait_owner(void *arg) {
    (void)arg;
    low_id = hrt__get_current();
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "multi-donor owner locks");
    mid_id = make_task(multi_wait_middle, stack_b, HRT_PRIO1);
    T_ASSERT_TRUE(mid_id >= 0, "created medium priority donor");
    hrt_yield();
    T_ASSERT_EQ_INT(1, lock_a.count_wait,
                    "medium donor is blocked before high donor arrives");
    high_id = make_task(multi_wait_high, stack_c, HRT_PRIO0);
    T_ASSERT_TRUE(high_id >= 0, "created high priority donor");
    hrt_yield();
    T_ASSERT_EQ_INT(2, lock_a.count_wait, "both priority donors blocked");
    T_ASSERT_EQ_INT(HRT_PRIO0, hrt__test_task_effective_priority(low_id),
                    "highest waiter boosts owner");
    for (int i = 0; i < 3; ++i) hrt_tick_from_isr();
    multi_wait_prio_after_expiry = hrt__test_task_effective_priority(low_id);
    multi_wait_remaining = lock_a.count_wait;
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_a),
                    "remaining FIFO donor receives ownership");
}
static void test_partial_donation_withdrawal(void) {
    reset_case(HRT_SCHED_PRIORITY);
    multi_wait_high_result = -7;
    multi_wait_mid_acquired = 0;
    multi_wait_prio_after_expiry = -7;
    multi_wait_remaining = -7;
    low_id = make_task(multi_wait_owner, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(low_id >= 0, "created multi-donor owner");
    hrt_start();
    T_ASSERT_EQ_INT(HRT_WAIT_TIMEOUT, multi_wait_high_result,
                    "highest donor times out without ownership");
    T_ASSERT_EQ_INT(HRT_PRIO1, multi_wait_prio_after_expiry,
                    "owner retains donation from remaining waiter");
    T_ASSERT_EQ_INT(1, multi_wait_remaining,
                    "only expired donor is unlinked");
    T_ASSERT_EQ_INT(1, multi_wait_mid_acquired,
                    "remaining donor eventually acquires mutex");
}

static void test_ready_mid_tail_migration(void) {
    reset_case(HRT_SCHED_PRIORITY_RR);
    const int a = make_task(competitor, stack_a, HRT_PRIO2);
    const int b = make_task(competitor, stack_b, HRT_PRIO2);
    const int d = make_task(competitor, stack_c, HRT_PRIO2);
    T_ASSERT_TRUE(a >= 0 && b >= 0 && d >= 0,
                  "created head, middle, and tail READY tasks");

    hrt_port_crit_enter();
    hrt__pi_set_effective_priority_locked(b, HRT_PRIO0);
    hrt__pi_set_effective_priority_locked(d, HRT_PRIO1);
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(a),
                    "unaffected FIFO head remains queued once");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(b),
                    "migrated FIFO middle remains queued once");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(d),
                    "migrated FIFO tail remains queued once");

    hrt__pi_set_effective_priority_locked(b, HRT_PRIO2);
    hrt__pi_set_effective_priority_locked(d, HRT_PRIO2);
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(a),
                    "head retains exactly one membership after restores");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(b),
                    "middle retains exactly one membership after restores");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(d),
                    "tail retains exactly one membership after restores");
    hrt_port_crit_exit();
}

static void cap_owner(void *arg) {
    (void)arg;
    static hrt_mutex_t held[HARDRT_MAX_MUTEXES_HELD + 1];
    for (int i = 0; i <= HARDRT_MAX_MUTEXES_HELD; ++i) hrt_mutex_init(&held[i]);
    for (int i = 0; i < HARDRT_MAX_MUTEXES_HELD; ++i) {
        T_ASSERT_EQ_INT(0, hrt_mutex_try_lock(&held[i]),
                        "acquisition below capacity succeeds");
    }
    result = hrt_mutex_try_lock(&held[HARDRT_MAX_MUTEXES_HELD]);
    T_ASSERT_EQ_INT(HRT_MUTEX_NO_OWNER, held[HARDRT_MAX_MUTEXES_HELD].owner,
                    "over-capacity attempt never published ownership");
    T_ASSERT_EQ_INT(HARDRT_MAX_MUTEXES_HELD,
                    hrt__test_task_held_mutex_count(hrt__get_current()),
                    "owned-mutex bookkeeping respects static cap");
    for (int i = HARDRT_MAX_MUTEXES_HELD; i > 0; --i) {
        T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&held[i - 1]),
                        "release held mutex after capacity test");
    }
    stop_test();
}
static void test_held_capacity(void) {
    reset_case(HRT_SCHED_PRIORITY);
    low_id = make_task(cap_owner, stack_a, HRT_PRIO1);
    T_ASSERT_TRUE(low_id >= 0, "created capacity owner");
    hrt_start();
    T_ASSERT_EQ_INT(-1, result, "attempt above held-mutex capacity rejected");
}

/* Retained quantum and explicit policy change while mutexes are held. */
static void policy_owner(void *arg) {
    (void)arg;
    const int me = hrt__get_current();
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "owner locked before policy change");
    hrt_set_policy(HRT_SCHED_RR);
    T_ASSERT_EQ_UINT(HRT_PRIO1, hrt__test_task_effective_priority(me),
                     "RR transition preserved task priority metadata");
    hrt_set_policy(HRT_SCHED_PRIORITY_RR);
    T_ASSERT_EQ_UINT(HRT_PRIO1, hrt__test_task_base_priority(me),
                     "policy changes preserved immutable base priority");
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_a),
                    "owner may unlock after round-trip policy changes");
    stop_test();
}
static void test_policy_switch_while_held(void) {
    reset_case(HRT_SCHED_PRIORITY);
    low_id = make_task(policy_owner, stack_a, HRT_PRIO1);
    T_ASSERT_TRUE(low_id >= 0, "created policy switch owner");
    hrt_start();
    T_ASSERT_EQ_INT(HRT_MUTEX_NO_OWNER, lock_a.owner,
                    "policy changes left mutex unlocked after release");
}

static void test_ready_migration_preserves_single_membership(void) {
    reset_case(HRT_SCHED_PRIORITY_RR);
    const int id = make_task(competitor, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(id >= 0, "created READY task for priority migration");

    hrt_port_crit_enter();
    hrt__pi_set_effective_priority_locked(id, HRT_PRIO0);
    T_ASSERT_EQ_INT(1, hrt__test_task_ready_queued(id),
                    "boosted READY task is still queued");
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(id),
                    "boosted READY task has exactly one membership");
    T_ASSERT_EQ_INT(HRT_PRIO0, hrt__test_task_effective_priority(id),
                    "boosted READY task moved into higher priority bucket");

    hrt__pi_set_effective_priority_locked(id, HRT_PRIO2);
    T_ASSERT_EQ_INT(1, hrt__test_ready_occurrences(id),
                    "restored READY task still has one membership");
    T_ASSERT_EQ_INT(HRT_PRIO2, hrt__test_task_effective_priority(id),
                    "READY task returned to base priority bucket");
    hrt_port_crit_exit();
}

static void quantum_owner(void *arg) {
    (void)arg;
    const int me = hrt__get_current();
    hrt_tick_from_isr();
    low_quantum_before = (int)hrt__test_task_slice_left(me);
    hrt_port_crit_enter();
    hrt__pi_set_effective_priority_locked(me, HRT_PRIO0);
    low_quantum_after = (int)hrt__test_task_slice_left(me);
    hrt__pi_set_effective_priority_locked(me, HRT_PRIO2);
    hrt_port_crit_exit();
    result = (int)hrt__test_task_slice_left(me);
    stop_test();
}
static void test_prio_rr_quantum_preserved_on_donation(void) {
    reset_case(HRT_SCHED_PRIORITY_RR);
    low_id = make_task(quantum_owner, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(low_id >= 0, "created RR-quantum task");
    hrt_start();
    T_ASSERT_EQ_INT(4, low_quantum_before, "external tick consumed one quantum");
    T_ASSERT_EQ_INT(4, low_quantum_after, "inheritance preserved remaining quantum");
    T_ASSERT_EQ_INT(4, result, "withdrawal preserved remaining quantum");
}

static volatile int fifo_order[2];
static volatile int fifo_count;
static void fifo_first(void *arg) {
    (void)arg;
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "first FIFO waiter acquires");
    fifo_order[fifo_count++] = 1;
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_a), "first FIFO waiter unlocks");
    hrt_yield();
}
static void fifo_second(void *arg) {
    (void)arg;
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "second FIFO waiter acquires");
    fifo_order[fifo_count++] = 2;
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_a), "second FIFO waiter unlocks");
    stop_test();
}
static void fifo_owner(void *arg) {
    (void)arg;
    T_ASSERT_EQ_INT(0, hrt_mutex_lock(&lock_a), "FIFO owner locks first");
    high_id = make_task(fifo_first, stack_b, HRT_PRIO0);
    mid_id = make_task(fifo_second, stack_c, HRT_PRIO0);
    T_ASSERT_TRUE(high_id >= 0 && mid_id >= 0, "created two FIFO waiters");
    hrt_yield();
    T_ASSERT_EQ_INT(2, lock_a.count_wait, "both waiters queued before handoff");
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&lock_a), "FIFO owner releases to first waiter");
}
static void test_multiple_waiters_preserve_fifo_handoff(void) {
    reset_case(HRT_SCHED_PRIORITY_RR);
    fifo_count = 0;
    fifo_order[0] = fifo_order[1] = 0;
    low_id = make_task(fifo_owner, stack_a, HRT_PRIO2);
    T_ASSERT_TRUE(low_id >= 0, "created FIFO owner");
    hrt_start();
    T_ASSERT_EQ_INT(2, fifo_count, "both mutex waiters completed");
    T_ASSERT_EQ_INT(1, fifo_order[0], "first FIFO waiter ran first");
    T_ASSERT_EQ_INT(2, fifo_order[1], "second FIFO waiter ran second");
}

static const test_case_t CASES[] = {
    {"Mutex PI: low/medium/high inversion under PRIORITY", test_fixed_priority_inversion},
    {"Mutex PI: low/medium/high inversion under PRIORITY_RR", test_priority_rr_inversion},
    {"Mutex PI: global RR intentionally ignores priority", test_global_rr_pi_is_metadata_only},
    {"Mutex PI: transitive donation through two owners", test_transitive_chain},
    {"Mutex PI: wait-for cycle is rejected", test_wait_for_cycle_rejected},
    {"Mutex PI: timeout withdraws donation", test_timeout_withdraws_donation},
    {"Mutex PI: timeout preserves remaining donor", test_partial_donation_withdrawal},
    {"Mutex PI: READY middle/tail reprioritization", test_ready_mid_tail_migration},
    {"Mutex PI: held capacity rejects excess mutex", test_held_capacity},
    {"Mutex PI: runtime policy switch preserves ownership", test_policy_switch_while_held},
    {"Mutex PI: READY priority migration preserves membership", test_ready_migration_preserves_single_membership},
    {"Mutex PI: PRIORITY_RR retains quantum on priority change", test_prio_rr_quantum_preserved_on_donation},
    {"Mutex PI: multiple waiters preserve FIFO handoff", test_multiple_waiters_preserve_fifo_handoff}
};

const test_case_t *get_tests_mutex_pi(int *out_count) {
    if (out_count != NULL)
        *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
