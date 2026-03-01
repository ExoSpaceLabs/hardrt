/* Tests for semaphore behavior: binary (legacy) and counting. */
#include "test_common.h"
#include "hardrt_sem.h"

/* ---- Case 1: basic try/take/give without blocking ---- */
static void test_sem_try_and_give_basic(void) {
    hrt__test_reset_scheduler_state();

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int r = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, r, "hrt_init ok");

    hrt_sem_t s;
    hrt_sem_init(&s, 1);
    r = hrt_sem_try_take(&s);
    T_ASSERT_EQ_INT(0, r, "try_take should succeed when initially available");

    r = hrt_sem_try_take(&s);
    T_ASSERT_EQ_INT(-1, r, "try_take should fail when already taken");

    r = hrt_sem_give(&s);
    T_ASSERT_EQ_INT(0, r, "give should succeed and make sem available again");

    r = hrt_sem_try_take(&s);
    T_ASSERT_EQ_INT(0, r, "try_take should succeed again after give");
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

/* ---- Case 2: blocking take wakes when given ---- */
static volatile int g_block_woke = 0;
static hrt_sem_t g_sem_block;

static void t_blocking_taker(void *arg) {
    (void) arg;
    for (;;) {
        /* Will block until giver releases */
        hrt_sem_take(&g_sem_block);
        g_block_woke = 1;
        /* Stop the scheduler after success */
        hrt__test_stop_scheduler();
        hrt_yield();
        hrt_sleep(1000);
    }
}

static void t_delayed_giver(void *arg) {
    (void) arg;
    for (;;) {
        /* Let the taker block first, then give */
        hrt_sleep(10);
        hrt_sem_give(&g_sem_block);
        /* Yield to allow higher-priority taker to run immediately */
        hrt_yield();
        /* sleep forever after one give */
        hrt_sleep(1000);
    }
}

static void test_sem_block_and_wake(void) {
    hrt__test_reset_scheduler_state();
    g_block_woke = 0;
    g_watchdog_tripped = 0;
    hrt_sem_init(&g_sem_block, 0);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (sem block)");

    static uint32_t swd[1024], s1[1024], s2[1024];
    /* Make taker higher priority (lower numeric value) so it preempts giver when woken */
    hrt_task_attr_t prio_hi = {.priority = HRT_PRIO0, .timeslice = 3};
    hrt_task_attr_t prio_lo = {.priority = HRT_PRIO2, .timeslice = 3};

    int wd = hrt_create_task(watchdog_task, (void *) (uintptr_t) 200, swd, 1024, &prio_lo);
    int t1 = hrt_create_task(t_blocking_taker, NULL, s1, 1024, &prio_hi);
    int t2 = hrt_create_task(t_delayed_giver, NULL, s2, 1024, &prio_lo);
    T_ASSERT_TRUE(wd>=0 && t1>=0 && t2>=0, "created watchdog, blocking taker and delayed giver");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in blocking test");
    T_ASSERT_EQ_INT(1, g_block_woke, "blocking taker should resume after give");
}

/* ---- Case 3: FIFO order of waiters at same priority ---- */
static hrt_sem_t g_sem_fifo;
static volatile int g_fifo_order[4];
static volatile int g_fifo_idx = 0;

static void waiter1(void *arg) {
    (void) arg;
    for (;;) {
        hrt_sem_take(&g_sem_fifo); /* blocks until given */
        if (g_fifo_idx < 3) g_fifo_order[++g_fifo_idx] = 1;
        if (g_fifo_idx >= 3) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_sleep(1000);
    }
}

static void waiter2(void *arg) {
    (void) arg;
    for (;;) {
        hrt_sem_take(&g_sem_fifo);
        if (g_fifo_idx < 3) g_fifo_order[++g_fifo_idx] = 2;
        if (g_fifo_idx >= 3) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_sleep(1000);
    }
}

static void waiter3(void *arg) {
    (void) arg;
    for (;;) {
        hrt_sem_take(&g_sem_fifo);
        if (g_fifo_idx < 3) g_fifo_order[++g_fifo_idx] = 3;
        if (g_fifo_idx >= 3) {
            hrt__test_stop_scheduler();
            hrt_yield();
        }
        hrt_sleep(1000);
    }
}

static void giver_task(void *arg) {
    (void) arg;
    for (;;) {
        /* Give three times with small delays to release waiters one by one */
        hrt_sleep(5);
        hrt_sem_give(&g_sem_fifo);
        hrt_sleep(5);
        hrt_sem_give(&g_sem_fifo);
        hrt_sleep(5);
        hrt_sem_give(&g_sem_fifo);
        hrt_sleep(1000);
    }
}

static void test_sem_fifo_wait_order(void) {
    hrt__test_reset_scheduler_state();
    for (int i = 0; i < 4; ++i) g_fifo_order[i] = 0;
    g_fifo_idx = 0;
    g_watchdog_tripped = 0;
    hrt_sem_init(&g_sem_fifo, 0);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 4};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (sem fifo)");

    static uint32_t swd[1024], s1[1024], s2[1024], s3[1024], sg[1024];
    hrt_task_attr_t a = {.priority = HRT_PRIO1, .timeslice = 2};
    /* Create waiters first so they queue in creation order */
    int id1 = hrt_create_task(waiter1, NULL, s1, 1024, &a);
    int id2 = hrt_create_task(waiter2, NULL, s2, 1024, &a);
    int id3 = hrt_create_task(waiter3, NULL, s3, 1024, &a);
    /* Giver to release them sequentially */
    int idg = hrt_create_task(giver_task, NULL, sg, 1024, &a);
    int wd = hrt_create_task(watchdog_task, (void *) (uintptr_t) 300, swd, 1024, &a);
    T_ASSERT_TRUE(id1>=0 && id2>=0 && id3>=0 && idg>=0 && wd>=0, "created three waiters, giver and watchdog");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in FIFO test");
    T_ASSERT_EQ_INT(1, g_fifo_order[1], "first released should be waiter1");
    T_ASSERT_EQ_INT(2, g_fifo_order[2], "second released should be waiter2");
    T_ASSERT_EQ_INT(3, g_fifo_order[3], "third released should be waiter3");
}

/* ---- Case 4: double give keeps semaphore binary (no overflow) ---- */
static void test_sem_double_give_binary(void) {
    hrt__test_reset_scheduler_state();

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok");

    hrt_sem_t s;
    hrt_sem_init(&s, 0);
    int rc = hrt_sem_give(&s);
    T_ASSERT_EQ_INT(0, rc, "first give should succeed from empty");
    rc = hrt_sem_give(&s);
    T_ASSERT_EQ_INT(0, rc, "second give should also succeed but keep binary state");
    int t1 = hrt_sem_try_take(&s);
    T_ASSERT_EQ_INT(0, t1, "after double give, first try_take succeeds");
    int t2 = hrt_sem_try_take(&s);
    T_ASSERT_EQ_INT(-1, t2, "after consuming once, second try_take fails (binary)");
}

/* ---- Case 5: give_from_isr wakes waiter and sets need_switch ---- */
static volatile int g_isr_woke = 0;
static hrt_sem_t g_sem_isr;

static void t_isr_waiter(void *arg) {
    (void) arg;
    for (;;) {
        hrt_sem_take(&g_sem_isr); /* blocks until ISR-style give */
        g_isr_woke = 1;
        hrt__test_stop_scheduler();
        hrt_yield();
        hrt_sleep(1000);
    }
}

static void t_isr_giver(void *arg) {
    (void) arg;
    for (;;) {
        hrt_sleep(10);
        int need = 0;
        hrt_sem_give_from_isr(&g_sem_isr, &need);
        /* After ISR give, a switch is only pended; yield so scheduler can run waiter. */
        T_ASSERT_EQ_INT(1, need, "give_from_isr should set need_switch when a waiter exists");
        hrt_yield();
        hrt_sleep(1000);
    }
}

static void test_sem_give_from_isr_sets_need_switch_and_wakes(void) {
    hrt__test_reset_scheduler_state();
    g_isr_woke = 0;
    g_watchdog_tripped = 0;
    hrt_sem_init(&g_sem_isr, 0);

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "hrt_init ok (sem isr give)");

    static uint32_t swd[1024], sw[1024], sg[1024];
    hrt_task_attr_t prio_hi = {.priority = HRT_PRIO0, .timeslice = 3}; /* waiter higher prio */
    hrt_task_attr_t prio_lo = {.priority = HRT_PRIO2, .timeslice = 3};

    int wd = hrt_create_task(watchdog_task, (void *) (uintptr_t) 250, swd, 1024, &prio_lo);
    int tw = hrt_create_task(t_isr_waiter, NULL, sw, 1024, &prio_hi);
    int tg = hrt_create_task(t_isr_giver, NULL, sg, 1024, &prio_lo);
    T_ASSERT_TRUE(wd>=0 && tw>=0 && tg>=0, "created waiter, isr-giver and watchdog");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in ISR give test");
    T_ASSERT_EQ_INT(1, g_isr_woke, "waiter should wake after give_from_isr");
}



/* ---- Case 6: counting semaphore accumulates and saturates ---- */
static void test_sem_counting_accumulates_and_saturates(void) {
    hrt__test_reset_scheduler_state();

    /* Initialize HRT to ensure port-specific critical section state is valid */
    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int r = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, r, "hrt_init ok");

    hrt_sem_t s;
    hrt_sem_init_counting(&s, 0, 3);

    /* Give more than max_count. Should saturate at 3. */
    r = hrt_sem_give(&s); T_ASSERT_EQ_INT(0, r, "give #1 ok");
    r = hrt_sem_give(&s); T_ASSERT_EQ_INT(0, r, "give #2 ok");
    r = hrt_sem_give(&s); T_ASSERT_EQ_INT(0, r, "give #3 ok");
    r = hrt_sem_give(&s); T_ASSERT_EQ_INT(0, r, "give #4 ok (saturates)");
    r = hrt_sem_give(&s); T_ASSERT_EQ_INT(0, r, "give #5 ok (saturates)");

    /* We should be able to take exactly 3 tokens. */
    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(0, r, "take #1 (of 3)");
    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(0, r, "take #2 (of 3)");
    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(0, r, "take #3 (of 3)");
    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(-1, r, "take #4 should fail (empty)");
}

/* ---- Case 7: counting semaphore init clamps to max_count ---- */
static void test_sem_counting_init_clamps(void) {
    hrt__test_reset_scheduler_state();

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int r = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, r, "hrt_init ok");

    hrt_sem_t s;
    hrt_sem_init_counting(&s, 10, 3); /* init > max -> clamp */

    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(0, r, "take #1 (of 3)");
    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(0, r, "take #2 (of 3)");
    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(0, r, "take #3 (of 3)");
    r = hrt_sem_try_take(&s); T_ASSERT_EQ_INT(-1, r, "take #4 should fail (clamped)");
}

/* ---- Case 8: counting semaphore give wakes waiter (handoff) ---- */
static volatile int g_count_woke = 0;
static volatile int g_count_try_after = -99;
static hrt_sem_t g_count_sem;

static void t_count_waiter(void *arg) {
    (void)arg;
    hrt_sem_take(&g_count_sem);
    g_count_woke = 1;
    /* The give that woke us is a direct handoff: there should be no extra token left. */
    g_count_try_after = hrt_sem_try_take(&g_count_sem);
    hrt__test_stop_scheduler();
    hrt_yield();
}

static void t_count_giver(void *arg) {
    (void)arg;
    hrt_sleep(50);
    hrt_sem_give(&g_count_sem);
    hrt_yield();
}

static void test_sem_counting_wake_is_handoff(void) {
    hrt__test_reset_scheduler_state();
    g_count_woke = 0;
    g_count_try_after = -99;

    hrt_sem_init_counting(&g_count_sem, 0, 5);

    static uint32_t sw[1024], sg[1024], swd[1024];
    hrt_task_attr_t prio_hi = {.priority = HRT_PRIO0, .timeslice = 3};
    hrt_task_attr_t prio_lo = {.priority = HRT_PRIO2, .timeslice = 3};

    int wd = hrt_create_task(watchdog_task, (void *) (uintptr_t) 250, swd, 1024, &prio_lo);
    int tw = hrt_create_task(t_count_waiter, NULL, sw, 1024, &prio_hi);
    int tg = hrt_create_task(t_count_giver, NULL, sg, 1024, &prio_lo);
    T_ASSERT_TRUE(wd>=0 && tw>=0 && tg>=0, "created waiter, giver and watchdog");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in counting handoff test");
    T_ASSERT_EQ_INT(1, g_count_woke, "waiter should wake after give");
    T_ASSERT_EQ_INT(-1, g_count_try_after, "no extra token should remain after handoff");
}

/* ---- Case 9: counting semaphore multiple waiters wake in FIFO order ---- */
static volatile int g_multi_woke_count = 0;
static volatile int g_multi_order[4] = {0};
static hrt_sem_t g_multi_sem;

static void t_multi_waiter(void *arg) {
    int id = (int)(uintptr_t)arg;
    hrt_sem_take(&g_multi_sem);
    int idx = ++g_multi_woke_count;
    if (idx < 4) g_multi_order[idx] = id;
    if (idx == 3) {
        hrt__test_stop_scheduler();
    }
    hrt_yield();
}

static void t_multi_giver(void *arg) {
    (void)arg;
    hrt_sleep(50);
    /* Give 3 tokens. Should wake all 3 waiters. */
    hrt_sem_give(&g_multi_sem);
    hrt_sem_give(&g_multi_sem);
    hrt_sem_give(&g_multi_sem);
    hrt_yield();
}

static void test_sem_counting_multi_waiter_fifo(void) {
    hrt__test_reset_scheduler_state();
    g_multi_woke_count = 0;
    memset((void*)g_multi_order, 0, sizeof(g_multi_order));

    hrt_config_t cfg = {.tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5};
    int r = hrt_init(&cfg);
    T_ASSERT_EQ_INT(0, r, "hrt_init ok");

    hrt_sem_init_counting(&g_multi_sem, 0, 5);

    static uint32_t sw1[1024], sw2[1024], sw3[1024], sg[1024], swd[1024];
    hrt_task_attr_t prio_hi = {.priority = HRT_PRIO0, .timeslice = 3};
    hrt_task_attr_t prio_lo = {.priority = HRT_PRIO2, .timeslice = 3};

    hrt_create_task(watchdog_task, (void *) (uintptr_t) 500, swd, 1024, &prio_lo);
    hrt_create_task(t_multi_waiter, (void*)1, sw1, 1024, &prio_hi);
    hrt_create_task(t_multi_waiter, (void*)2, sw2, 1024, &prio_hi);
    hrt_create_task(t_multi_waiter, (void*)3, sw3, 1024, &prio_hi);
    hrt_create_task(t_multi_giver, NULL, sg, 1024, &prio_lo);

    hrt_start();

    T_ASSERT_EQ_INT(0, g_watchdog_tripped, "watchdog should not trip in multi-waiter test");
    T_ASSERT_EQ_INT(3, g_multi_woke_count, "all 3 waiters should have woken");
    T_ASSERT_EQ_INT(1, g_multi_order[1], "first woken should be task 1");
    T_ASSERT_EQ_INT(2, g_multi_order[2], "second woken should be task 2");
    T_ASSERT_EQ_INT(3, g_multi_order[3], "third woken should be task 3");
}

static const test_case_t CASES[] = {
    {"Semaphore: try/take/give basic", test_sem_try_and_give_basic},
    {"Semaphore: blocking take wakes on give", test_sem_block_and_wake},
    {"Semaphore: FIFO wait order", test_sem_fifo_wait_order},
    {"Semaphore: double give remains binary", test_sem_double_give_binary},
    {"Semaphore: give_from_isr wakes and sets need_switch", test_sem_give_from_isr_sets_need_switch_and_wakes},
    {"Semaphore: accumulate shall saturate at set value",test_sem_counting_accumulates_and_saturates},
    {"Semaphore: init shall clamp to max_count",test_sem_counting_init_clamps},
    {"Semaphore: direct handoff shall fail after wake with 0 tokens",test_sem_counting_wake_is_handoff},
    {"Semaphore: counting FIFO wait order", test_sem_counting_multi_waiter_fifo}
};

const test_case_t *get_tests_semaphore(int *out_count) {
    if (out_count)*out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}