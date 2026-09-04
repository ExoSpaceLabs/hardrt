/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "hardrt.h"
#include "hardrt_time.h"
#include "hardrt_port_contract.h"

static int g_crit_depth = 0;
static sigset_t g_saved_mask;

typedef struct {
    ucontext_t ctx;
    void *stk_ptr;
    size_t stk_bytes;
    int valid;
} _port_ctx_t;

static _port_ctx_t g_ctxs[HARDRT_MAX_TASKS];
static ucontext_t g_sched_ctx;
static volatile sig_atomic_t g_switch_pending = 0;
static sigset_t g_sigalrm_set;
static struct itimerval g_tick_timer;

#ifdef HARDRT_TEST_HOOKS
static volatile sig_atomic_t g_test_stop = 0;
static volatile unsigned long long g_idle_counter = 0;
static volatile sig_atomic_t g_test_fail_next_prepare = 0;

void hrt__test_stop_scheduler(void) { g_test_stop = 1; }
void hrt__test_reset_scheduler_state(void) {
    g_test_stop = 0;
    g_switch_pending = 1;
    g_test_fail_next_prepare = 0;
}
void hrt__test_idle_counter_reset(void) { g_idle_counter = 0; }
unsigned long long hrt__test_idle_counter_value(void) { return g_idle_counter; }
void hrt__test_fail_next_prepare_task_stack(void) { g_test_fail_next_prepare = 1; }

void hrt__test_fast_forward_ticks(uint32_t delta) {
    sigset_t old;
    sigprocmask(SIG_BLOCK, &g_sigalrm_set, &old);
    for (uint32_t i = 0; i < delta; ++i) hrt__tick_isr();
    sigprocmask(SIG_SETMASK, &old, NULL);
}

void hrt__test_block_sigalrm(void) {
    sigprocmask(SIG_BLOCK, &g_sigalrm_set, NULL);
}
void hrt__test_unblock_sigalrm(void) {
    sigprocmask(SIG_UNBLOCK, &g_sigalrm_set, NULL);
}
#endif

static inline void block_sigalrm(sigset_t *old) {
    sigprocmask(SIG_BLOCK, &g_sigalrm_set, old);
}

static inline void unblock_sigalrm(const sigset_t *old) {
    sigprocmask(SIG_SETMASK, old, NULL);
}

void hrt__task_trampoline(void) {
    const int id = hrt__get_current();
    const _hrt_tcb_t *t = hrt__tcb(id);
    t->entry(t->arg);
    hrt_task_delete();
}

int hrt_port_prepare_task_stack(const int id, void (*tramp)(void),
                                uint32_t *stack_base, const size_t words) {
    (void)tramp;
#ifdef HARDRT_TEST_HOOKS
    if (g_test_fail_next_prepare != 0) {
        g_test_fail_next_prepare = 0;
        g_ctxs[id].valid = 0;
        return -1;
    }
#endif

    const size_t bytes = words * sizeof(uint32_t);
    if (getcontext(&g_ctxs[id].ctx) != 0) {
        g_ctxs[id].valid = 0;
        return -1;
    }
    g_ctxs[id].ctx.uc_stack.ss_sp = (void *)stack_base;
    g_ctxs[id].ctx.uc_stack.ss_size = bytes;
    g_ctxs[id].ctx.uc_link = &g_sched_ctx;
    makecontext(&g_ctxs[id].ctx, hrt__task_trampoline, 0);
    g_ctxs[id].stk_ptr = (void *)stack_base;
    g_ctxs[id].stk_bytes = bytes;
    g_ctxs[id].valid = 1;
    return 0;
}

static void _tick_sighandler(const int signo) {
    (void)signo;
    hrt__tick_isr();
    g_switch_pending = 1;
}

int hrt_port_configure_tick(const uint32_t tick_hz) {
    sigemptyset(&g_sigalrm_set);
    sigaddset(&g_sigalrm_set, SIGALRM);

    /* Reconfiguration never leaves a timer from a previous hosted run armed. */
    const struct itimerval stopped = {0};
    if (setitimer(ITIMER_REAL, &stopped, NULL) != 0) return -1;
    memset(&g_tick_timer, 0, sizeof(g_tick_timer));

    if (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL) return 0;
    if (tick_hz == 0u) return -1;

    const uint64_t period_us = 1000000ULL / (uint64_t)tick_hz;
    if (period_us == 0u) return -1;

    struct sigaction sa = {0};
    sa.sa_handler = _tick_sighandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &sa, NULL) != 0) return -1;

    g_tick_timer.it_value.tv_sec = (time_t)(period_us / 1000000ULL);
    g_tick_timer.it_value.tv_usec = (suseconds_t)(period_us % 1000000ULL);
    g_tick_timer.it_interval = g_tick_timer.it_value;
    return 0;
}

void hrt_port_idle_wait(void) {
#ifdef HARDRT_TEST_HOOKS
    g_idle_counter++;
#endif
    const struct timespec ts = {0, 1 * 1000 * 1000};
    nanosleep(&ts, NULL);
}

void hrt__pend_context_switch(void) {
    g_switch_pending = 1;
}

void hrt_port_yield_to_scheduler(void) {
    const int cur = hrt__get_current();
    if (cur < 0 || cur == HRT_IDLE_ID || !g_ctxs[cur].valid) return;
    sigset_t old;
    block_sigalrm(&old);
    swapcontext(&g_ctxs[cur].ctx, &g_sched_ctx);
    unblock_sigalrm(&old);
}

void hrt_port_enter_scheduler(void) {
    int tick_armed = (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL);
    g_switch_pending = 1;

    for (;;) {
#ifdef HARDRT_TEST_HOOKS
        if (g_test_stop) {
            struct itimerval it = {0};
            setitimer(ITIMER_REAL, &it, NULL);
            return;
        }
#endif
        if (!g_switch_pending) {
            hrt_port_idle_wait();
            continue;
        }
        g_switch_pending = 0;

        sigset_t old;
        block_sigalrm(&old);

        const int next = hrt__pick_next_ready();
        if (next < 0 || next == HRT_IDLE_ID) {
            unblock_sigalrm(&old);
            hrt_port_idle_wait();
            continue;
        }

        hrt__set_current(next);
        if (!tick_armed) {
            /* Arm the hosted periodic tick only after a valid current task has
             * been selected. SIGALRM is blocked in scheduler context here, so
             * no tick can observe g_current == -1 during startup. */
            if (setitimer(ITIMER_REAL, &g_tick_timer, NULL) != 0) {
                unblock_sigalrm(&old);
                return;
            }
            tick_armed = 1;
        }
        swapcontext(&g_sched_ctx, &g_ctxs[next].ctx);
        hrt__on_scheduler_entry();
        unblock_sigalrm(&old);
    }
}

void hrt_port_crit_enter(void) {
    if (g_crit_depth++ == 0) sigprocmask(SIG_BLOCK, &g_sigalrm_set, NULL);
}

void hrt_port_crit_exit(void) {
    if (g_crit_depth <= 0) {
        g_crit_depth = 0;
        return;
    }
    if (--g_crit_depth == 0) sigprocmask(SIG_UNBLOCK, &g_sigalrm_set, NULL);
}

void hrt_port_sp_valid(const uintptr_t sp) {
    (void)sp;
}

void hrt__init_idle_task(void) {
    _hrt_tcb_t *idle = hrt__tcb(HRT_IDLE_ID);
    if (idle == NULL) {
        hrt_error(ERR_TCB_NULL);
        return;
    }
    idle->state = HRT_READY;
    idle->prio = 0u;
    idle->timeslice_cfg = 0u;
    idle->slice_left = 0u;
}
