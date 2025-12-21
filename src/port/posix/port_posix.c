/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>   /* nanosleep */

#include "hardrt.h"
#include "hardrt_time.h"
#include "hardrt_port_int.h"


static int g_crit_depth = 0;
static sigset_t g_saved_mask;

/* ---- Core-private hooks ---- */
void hrt__tick_isr(void);

int hrt__pick_next_ready(void);

void hrt__make_ready(int id);

void hrt__on_scheduler_entry(void);

int hrt__get_current(void);

void hrt__set_current(int id);

_hrt_tcb_t *hrt__tcb(int id);

/* ---- Port state ---- */
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

#ifdef HARDRT_TEST_HOOKS
 static volatile sig_atomic_t g_test_stop = 0;
 static volatile unsigned long long g_idle_counter = 0;
 void hrt__test_stop_scheduler(void) { g_test_stop = 1; }
 /* Test helper: reset scheduler test state between test cases */
 void hrt__test_reset_scheduler_state(void) {
     g_test_stop = 0;
     g_switch_pending = 1;
 }
 
 /* Idle counter helpers (tests may inspect liveness) */
 void hrt__test_idle_counter_reset(void) { g_idle_counter = 0; }
 unsigned long long hrt__test_idle_counter_value(void) { return g_idle_counter; }
 
 /* Fast-forward ticks for wraparound tests: mask SIGALRM and call core tick. */
 void hrt__test_fast_forward_ticks(uint32_t delta) {
     sigset_t old;
     /* block SIGALRM directly to avoid re-entrancy during synthetic ticks */
     sigprocmask(SIG_BLOCK, &g_sigalrm_set, &old);
     for (uint32_t i = 0; i < delta; ++i) { hrt__tick_isr(); }
     sigprocmask(SIG_SETMASK, &old, NULL);
 }
 
 /* Allow tests to block/unblock SIGALRM around sensitive regions */
 void hrt__test_block_sigalrm(void) {
     sigprocmask(SIG_BLOCK, &g_sigalrm_set, NULL);
 }
 void hrt__test_unblock_sigalrm(void) {
     sigprocmask(SIG_UNBLOCK, &g_sigalrm_set, NULL);
 }
 
 /* Access to tick for tests (delegates to core helpers) */
 void hrt__test_set_tick(uint32_t v);
 uint32_t hrt__test_get_tick(void);
 #endif

/* ---- Helpers to mask/unmask SIGALRM around critical regions ---- */
static inline void block_sigalrm(sigset_t *old) {
    sigprocmask(SIG_BLOCK, &g_sigalrm_set, old);
}

static inline void unblock_sigalrm(const sigset_t *old) {
    sigprocmask(SIG_SETMASK, old, NULL);
}

/* ---- Task trampoline ---- */
void hrt__task_trampoline(void) {
    const int id = hrt__get_current();
    const _hrt_tcb_t *t = hrt__tcb(id);
    t->entry(t->arg);
    /* If the task returns, request a rescheduling and jump back */
    g_switch_pending = 1;
    swapcontext(&g_ctxs[id].ctx, &g_sched_ctx);
    /* not reached */
}

/* Prepare ucontext for the task using the provided stack */
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t *stack_base, size_t words) {
    (void) tramp; /* we use hrt__task_trampoline directly */
    const size_t bytes = words * sizeof(uint32_t);
    getcontext(&g_ctxs[id].ctx);
    g_ctxs[id].ctx.uc_stack.ss_sp = (void *) stack_base;
    g_ctxs[id].ctx.uc_stack.ss_size = bytes;
    g_ctxs[id].ctx.uc_link = &g_sched_ctx; /* return to scheduler if task exits */
    makecontext(&g_ctxs[id].ctx, hrt__task_trampoline, 0);
    g_ctxs[id].stk_ptr = (void *) stack_base;
    g_ctxs[id].stk_bytes = bytes;
    g_ctxs[id].valid = 1;
}

/* Tick handler: only set a flag; do not swap here */
static void _tick_sighandler(int signo) {
    (void) signo;
    hrt__tick_isr();
    g_switch_pending = 1;
}

/* Start periodic SIGALRM at the requested Hz */
void hrt_port_start_systick(const uint32_t tick_hz) {
    /* If external tick is selected, do not start SIGALRM timer. */
    if (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL) {
        return;
    }
    sigemptyset(&g_sigalrm_set);
    sigaddset(&g_sigalrm_set, SIGALRM);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = _tick_sighandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval it;
    memset(&it, 0, sizeof it);
    long usec = (tick_hz ? (1000000L / (long) tick_hz) : 1000L);
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = usec;
    it.it_interval = it.it_value;
    setitimer(ITIMER_REAL, &it, NULL);
}

/* Idle hook: tiny nap to avoid 100% CPU */
void hrt_port_idle_wait(void) {
#ifdef HARDRT_TEST_HOOKS
    g_idle_counter++;
#endif
    const struct timespec ts = {0, 1 * 1000 * 1000}; /* 1 ms */
    nanosleep(&ts, NULL);
}

/* ISR-safe: just set a flag */
void hrt__pend_context_switch(void) {
    g_switch_pending = 1;
}

/* Task-context only: hop into the scheduler with SIGALRM masked */
void hrt_port_yield_to_scheduler(void) {
    const int cur = hrt__get_current();
    if (cur < 0 || !g_ctxs[cur].valid) return;
    sigset_t old;
    block_sigalrm(&old);
    swapcontext(&g_ctxs[cur].ctx, &g_sched_ctx);
    unblock_sigalrm(&old);
}

/* Scheduler loop: pick and switch, with SIGALRM masked during critical sections */
void hrt_port_enter_scheduler(void) {
    for (;;) {
#ifdef HARDRT_TEST_HOOKS
        if (g_test_stop) {
            /* Disable timer and exit scheduler loop for tests */
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
        if (next < 0) {
            unblock_sigalrm(&old);
            hrt_port_idle_wait();
            continue;
        }

        hrt__set_current(next);
        /* Jump from scheduler to task; task will swap back when it yields/sleeps */
        swapcontext(&g_sched_ctx, &g_ctxs[next].ctx);

        /* We are back in scheduler context with SIGALRM still masked. */
        hrt__on_scheduler_entry();

        unblock_sigalrm(&old);
        /* when we return here, a yield/sleep/time-slice triggered it */
    }
}

void hrt_port_crit_enter(void) {
    if (g_crit_depth++ == 0) {
        /* Block SIGALRM; we don't attempt to restore an arbitrary previous mask here.
           Critical sections are short; on final exit we simply unmask SIGALRM. */
        sigprocmask(SIG_BLOCK, &g_sigalrm_set, NULL);
    }
}

void hrt_port_crit_exit(void) {
    if (--g_crit_depth == 0) {
        /* Unblock SIGALRM when leaving the outermost critical section. */
        sigprocmask(SIG_UNBLOCK, &g_sigalrm_set, NULL);
    }
}

void hrt_port_sp_valid(const uint32_t sp)
{
    (void)sp;
    /* POSIX “stacks” are host stacks / malloc etc.; no HW limit. */
}
void hrt__init_idle_task(void) {

}