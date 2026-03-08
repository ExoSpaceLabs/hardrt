/* Tests for mutex behavior: ownership, blocking handoff and FIFO order.
 *
 * Assumptions about the mutex API:
 *   - hrt_mutex_init(hrt_mutex_t *m)
 *   - hrt_mutex_lock(hrt_mutex_t *m)
 *   - hrt_mutex_try_lock(hrt_mutex_t *m)
 *   - hrt_mutex_unlock(hrt_mutex_t *m)
 *
 * Expected semantics:
 *   - non-recursive mutex
 *   - only owner can unlock
 *   - blocking lock wakes through direct ownership handoff
 *   - waiters are resumed FIFO
 */
#include "test_common.h"
#include "hardrt_mutex.h"

/* ---- Utility: watchdog to avoid infinite tests ---- */
static volatile int g_watchdog_tripped = 0;

static void watchdog_task(void *arg) {
    uint32_t ticks = (uint32_t)(uintptr_t)arg;
    for (;;) {
        hrt_sleep(ticks);
        g_watchdog_tripped = 1;
        hrt__test_stop_scheduler();
        hrt_yield();
    }
}

/* ---- Case 1: basic try_lock / unlock / re-lock ---- */
static void t_basic_worker(void *arg) {
    (void)arg;
    hrt_mutex_t m;
    hrt_mutex_init(&m);

    int r = hrt_mutex_try_lock(&m);
    T_ASSERT_EQ_INT(0, r, "try_lock should succeed when initially unlocked");

    r = hrt_mutex_unlock(&m);
    T_ASSERT_EQ_INT(0, r, "unlock should succeed for owner");

    r = hrt_mutex_try_lock(&m);
    T_ASSERT_EQ_INT(0, r, "try_lock should succeed again after unlock");

    r = hrt_mutex_unlock(&m);
    T_ASSERT_EQ_INT(0, r, "final unlock should succeed");
    hrt__test_stop_scheduler();
}

static void test_mutex_try_lock_and_unlock_basic(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t s1[1024];
    hrt_task_attr_t p = {.priority = HRT_PRIO1, .timeslice = 5};
    hrt_create_task(t_basic_worker, NULL, s1, 1024, &p);

    hrt_start();
}

/* ---- Case 2: recursive try_lock fails on non-recursive mutex ---- */
static void t_recursive_worker(void *arg) {
    (void)arg;
    hrt_mutex_t m;
    hrt_mutex_init(&m);

    int r = hrt_mutex_try_lock(&m);
    T_ASSERT_EQ_INT(0, r, "first try_lock should succeed");

    r = hrt_mutex_try_lock(&m);
    T_ASSERT_EQ_INT(-1, r, "second try_lock by same owner should fail on non-recursive mutex");

    r = hrt_mutex_unlock(&m);
    T_ASSERT_EQ_INT(0, r, "unlock after failed recursive lock should still succeed");
    hrt__test_stop_scheduler();
}

static void test_mutex_recursive_try_lock_fails(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t s1[1024];
    hrt_task_attr_t p = {.priority = HRT_PRIO1, .timeslice = 5};
    hrt_create_task(t_recursive_worker, NULL, s1, 1024, &p);

    hrt_start();
}

/* ---- Case 3: blocking lock wakes when owner unlocks ---- */
static volatile int g_lock_woke = 0;
static hrt_mutex_t g_mutex_block;

static void t_blocking_locker(void *arg) {
    (void)arg;
    hrt_mutex_lock(&g_mutex_block);
    g_lock_woke = 1;
    hrt_mutex_unlock(&g_mutex_block);
    hrt__test_stop_scheduler();
}

static void t_initial_owner_then_unlock(void *arg) {
    (void)arg;
    int r = hrt_mutex_lock(&g_mutex_block);
    T_ASSERT_EQ_INT(0, r, "initial owner should lock mutex");
    hrt_sleep(20);
    r = hrt_mutex_unlock(&g_mutex_block);
    T_ASSERT_EQ_INT(0, r, "initial owner should unlock mutex and wake waiter");
    
    /* Wait for waiter to finish */
    hrt_sleep(100);
}

static void test_mutex_block_and_wake(void) {
    hrt__test_reset_scheduler_state();
    g_lock_woke = 0;
    g_watchdog_tripped = 0;
    hrt_mutex_init(&g_mutex_block);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t swd[1024], s1[1024], s2[1024];
    hrt_task_attr_t p = {.priority = HRT_PRIO1, .timeslice = 5};
    hrt_task_attr_t p_wd = {.priority = HRT_PRIO2, .timeslice = 5};

    /* Create owner first - same priority, so it runs first in RR/Priority-RR usually, 
       but we make it acquire then sleep. */
    hrt_create_task(t_initial_owner_then_unlock, NULL, s1, 1024, &p);
    hrt_create_task(t_blocking_locker, NULL, s2, 1024, &p);
    hrt_create_task(watchdog_task, (void *)(uintptr_t)300, swd, 1024, &p_wd);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in blocking mutex test");
    T_ASSERT_EQ_INT(1, g_lock_woke, "blocking waiter should resume after owner unlocks");
}

/* ---- Case 4: FIFO order of waiters at same priority ---- */
static hrt_mutex_t g_mutex_fifo;
static volatile int g_fifo_order[4];
static volatile int g_fifo_idx = 0;

static void waiter1(void *arg) {
    (void)arg;
    hrt_mutex_lock(&g_mutex_fifo);
    if (g_fifo_idx < 3) g_fifo_order[++g_fifo_idx] = 1;
    hrt_mutex_unlock(&g_mutex_fifo);
}

static void waiter2(void *arg) {
    (void)arg;
    hrt_mutex_lock(&g_mutex_fifo);
    if (g_fifo_idx < 3) g_fifo_order[++g_fifo_idx] = 2;
    hrt_mutex_unlock(&g_mutex_fifo);
}

static void waiter3(void *arg) {
    (void)arg;
    hrt_mutex_lock(&g_mutex_fifo);
    if (g_fifo_idx < 3) g_fifo_order[++g_fifo_idx] = 3;
    hrt_mutex_unlock(&g_mutex_fifo);
    hrt__test_stop_scheduler();
}

static void owner_fifo_task(void *arg) {
    (void)arg;
    int r = hrt_mutex_lock(&g_mutex_fifo);
    T_ASSERT_EQ_INT(0, r, "FIFO owner should lock mutex first");
    
    /* Sleep enough for all waiters to block */
    hrt_sleep(100);
    
    r = hrt_mutex_unlock(&g_mutex_fifo);
    T_ASSERT_EQ_INT(0, r, "FIFO owner should release mutex to first waiter");
}

static void test_mutex_fifo_wait_order(void) {
    hrt__test_reset_scheduler_state();
    for (int i = 0; i < 4; ++i) g_fifo_order[i] = 0;
    g_fifo_idx = 0;
    g_watchdog_tripped = 0;
    hrt_mutex_init(&g_mutex_fifo);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 10};
    hrt_init(&cfg);

    static uint32_t swd[1024], so[1024], s1[1024], s2[1024], s3[1024];
    hrt_task_attr_t same = {.priority = HRT_PRIO1, .timeslice = 5};
    hrt_task_attr_t low  = {.priority = HRT_PRIO2, .timeslice = 5};

    /* Create owner first */
    hrt_create_task(owner_fifo_task, NULL, so, 1024, &same);
    /* Create waiters in order */
    hrt_create_task(waiter1, NULL, s1, 1024, &same);
    hrt_create_task(waiter2, NULL, s2, 1024, &same);
    hrt_create_task(waiter3, NULL, s3, 1024, &same);
    
    hrt_create_task(watchdog_task, (void *)(uintptr_t)500, swd, 1024, &low);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in FIFO mutex test");
    T_ASSERT_EQ_INT(1, g_fifo_order[1], "first released should be waiter1");
    T_ASSERT_EQ_INT(2, g_fifo_order[2], "second released should be waiter2");
    T_ASSERT_EQ_INT(3, g_fifo_order[3], "third released should be waiter3");
}

/* ---- Case 5: non-owner unlock fails ---- */
static volatile int g_non_owner_unlock_rc = 0;
static volatile int g_owner_unlock_rc = 0;
static hrt_mutex_t g_mutex_owner_check;

static void t_owner_holds_mutex(void *arg) {
    (void)arg;
    for (;;) {
        int r = hrt_mutex_lock(&g_mutex_owner_check);
        T_ASSERT_EQ_INT(0, r, "owner task should lock mutex");
        hrt_sleep(20);
        g_owner_unlock_rc = hrt_mutex_unlock(&g_mutex_owner_check);
        hrt__test_stop_scheduler();
        hrt_yield();
        hrt_sleep(1000);
    }
}

static void t_non_owner_attempts_unlock(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sleep(5);
        g_non_owner_unlock_rc = hrt_mutex_unlock(&g_mutex_owner_check);
        hrt_sleep(1000);
    }
}

static void test_mutex_non_owner_unlock_fails(void) {
    hrt__test_reset_scheduler_state();
    g_watchdog_tripped = 0;
    g_non_owner_unlock_rc = 1234;
    g_owner_unlock_rc = 1234;
    hrt_mutex_init(&g_mutex_owner_check);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (non-owner unlock)");

    static uint32_t swd[1024], so[1024], sn[1024];
    hrt_task_attr_t prio_hi = {.priority = HRT_PRIO1, .timeslice = 3};
    hrt_task_attr_t prio_lo = {.priority = HRT_PRIO2, .timeslice = 3};

    int owner = hrt_create_task(t_owner_holds_mutex, NULL, so, 1024, &prio_hi);
    int intruder = hrt_create_task(t_non_owner_attempts_unlock, NULL, sn, 1024, &prio_lo);
    int wd = hrt_create_task(watchdog_task, (void *)(uintptr_t)200, swd, 1024, &prio_lo);
    T_ASSERT_TRUE(owner >= 0 && intruder >= 0 && wd >= 0, "created owner, intruder and watchdog");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in non-owner unlock test");
    T_ASSERT_EQ_INT(-1, g_non_owner_unlock_rc, "non-owner unlock should fail");
    T_ASSERT_EQ_INT(0, g_owner_unlock_rc, "real owner unlock should succeed");
}

/* ---- Case 6: direct handoff means waiter already owns mutex on wake ---- */
static volatile int g_handoff_lock_rc = 0;
static volatile int g_handoff_try_after = 0;
static hrt_mutex_t g_mutex_handoff;

static void t_handoff_waiter(void *arg) {
    (void)arg;
    g_handoff_lock_rc = hrt_mutex_lock(&g_mutex_handoff);
    /* If wakeup is a direct handoff, this task already owns the mutex here,
     * therefore a non-recursive try_lock must fail. */
    g_handoff_try_after = hrt_mutex_try_lock(&g_mutex_handoff);
    hrt_mutex_unlock(&g_mutex_handoff);
    hrt__test_stop_scheduler();
}

static void t_handoff_owner(void *arg) {
    (void)arg;
    hrt_mutex_lock(&g_mutex_handoff);
    hrt_sleep(50);
    hrt_mutex_unlock(&g_mutex_handoff);
    
    /* Wait for waiter to finish */
    hrt_sleep(100);
}

static void test_mutex_wake_is_direct_handoff(void) {
    hrt__test_reset_scheduler_state();
    g_watchdog_tripped = 0;
    g_handoff_lock_rc = 1234;
    g_handoff_try_after = 1234;
    hrt_mutex_init(&g_mutex_handoff);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t swd[1024], so[1024], sw[1024];
    hrt_task_attr_t p = {.priority = HRT_PRIO1, .timeslice = 5};

    hrt_create_task(t_handoff_owner, NULL, so, 1024, &p);
    hrt_create_task(t_handoff_waiter, NULL, sw, 1024, &p);
    hrt_create_task(watchdog_task, (void *)(uintptr_t)300, swd, 1024, &p);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in direct handoff test");
    T_ASSERT_EQ_INT(0, g_handoff_lock_rc, "waiter lock should eventually succeed");
    T_ASSERT_EQ_INT(-1, g_handoff_try_after, "try_lock after wake should fail because waiter already owns mutex");
}

/* ---- Case 7: mutex survives hrt_stop/init cycle if re-init correctly ---- */
static void t_idempotency_worker(void *arg) {
    (void)arg;
    hrt_mutex_t m;
    hrt_mutex_init(&m);
    T_ASSERT_EQ_INT(0, hrt_mutex_try_lock(&m), "lock in task");
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&m), "unlock in task");
    
    /* Re-init mutex */
    hrt_mutex_init(&m);
    T_ASSERT_EQ_INT(0, hrt_mutex_try_lock(&m), "lock after re-init");
    T_ASSERT_EQ_INT(0, hrt_mutex_unlock(&m), "unlock after re-init");
    hrt__test_stop_scheduler();
}

static void test_mutex_init_idempotency(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t s1[1024];
    hrt_task_attr_t p = {.priority = HRT_PRIO1, .timeslice = 5};
    hrt_create_task(t_idempotency_worker, NULL, s1, 1024, &p);

    hrt_start();
}

/* ---- Case 8: try_lock failure when already locked by another task ---- */
static volatile int g_other_locked = 0;
static hrt_mutex_t g_mutex_busy;

static void t_busy_owner(void *arg) {
    (void)arg;
    hrt_mutex_lock(&g_mutex_busy);
    g_other_locked = 1;
    hrt_sleep(50);
    hrt_mutex_unlock(&g_mutex_busy);
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void t_busy_tryer(void *arg) {
    (void)arg;
    while (!g_other_locked) hrt_yield();
    int r = hrt_mutex_try_lock(&g_mutex_busy);
    T_ASSERT_EQ_INT(-1, r, "try_lock should fail when owned by another task");
}

static void test_mutex_try_lock_fails_when_busy(void) {
    hrt__test_reset_scheduler_state();
    g_other_locked = 0;
    g_watchdog_tripped = 0;
    hrt_mutex_init(&g_mutex_busy);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    hrt_init(&cfg);

    static uint32_t swd[1024], s1[1024], s2[1024];
    hrt_task_attr_t p = {.priority = HRT_PRIO1, .timeslice = 5};

    hrt_create_task(t_busy_owner, NULL, s1, 1024, &p);
    hrt_create_task(t_busy_tryer, NULL, s2, 1024, &p);
    hrt_create_task(watchdog_task, (void *)(uintptr_t)200, swd, 1024, &p);

    hrt_start();
    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in busy try_lock test");
}

static const test_case_t CASES[] = {
    {"Mutex: try_lock / unlock basic", test_mutex_try_lock_and_unlock_basic},
    {"Mutex: recursive try_lock fails", test_mutex_recursive_try_lock_fails},
    {"Mutex: blocking lock wakes on unlock", test_mutex_block_and_wake},
    {"Mutex: FIFO wait order", test_mutex_fifo_wait_order},
    {"Mutex: non-owner unlock fails", test_mutex_non_owner_unlock_fails},
    {"Mutex: wake is direct handoff", test_mutex_wake_is_direct_handoff},
    {"Mutex: init idempotency", test_mutex_init_idempotency},
    {"Mutex: try_lock fails when busy", test_mutex_try_lock_fails_when_busy}
};

const test_case_t *get_tests_mutex(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}