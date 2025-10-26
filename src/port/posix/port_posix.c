/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>   /* nanosleep */

#include "heartos.h"
#include "heartos_time.h"

/* ---- Core-private hooks ---- */
int         hrt__pick_next_ready(void);
void        hrt__make_ready(int id);
int         hrt__get_current(void);
void        hrt__set_current(int id);

/* Mirror TCB fields used by the port */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    size_t    stack_words;
    void    (*entry)(void*);
    void*     arg;
    uint32_t  wake_tick;
    uint16_t  timeslice_cfg;
    uint16_t  slice_left;
    uint8_t   prio;
    uint8_t   state;
} _hrt_tcb_t;
_hrt_tcb_t* hrt__tcb(int id);

/* ---- Port state ---- */
typedef struct {
    ucontext_t ctx;
    void*      stk_ptr;
    size_t     stk_bytes;
    int        valid;
} _port_ctx_t;

static _port_ctx_t g_ctxs[HEARTOS_MAX_TASKS];
static ucontext_t  g_sched_ctx;
static volatile sig_atomic_t g_switch_pending = 0;
static sigset_t g_sigalrm_set;

/* ---- Helpers to mask/unmask SIGALRM around critical regions ---- */
static inline void block_sigalrm(sigset_t* old){
    sigprocmask(SIG_BLOCK, &g_sigalrm_set, old);
}
static inline void unblock_sigalrm(const sigset_t* old){
    sigprocmask(SIG_SETMASK, old, NULL);
}

/* ---- Task trampoline ---- */
void hrt__task_trampoline(void) {
    int id = hrt__get_current();
    _hrt_tcb_t* t = hrt__tcb(id);
    t->entry(t->arg);
    /* If the task returns, request a reschedule and jump back */
    g_switch_pending = 1;
    swapcontext(&g_ctxs[id].ctx, &g_sched_ctx);
    /* not reached */
}

/* Prepare ucontext for the task using the provided stack */
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t* stack_base, size_t words)
{
    (void)tramp; /* we use hrt__task_trampoline directly */
    size_t bytes = words * sizeof(uint32_t);
    getcontext(&g_ctxs[id].ctx);
    g_ctxs[id].ctx.uc_stack.ss_sp   = (void*)stack_base;
    g_ctxs[id].ctx.uc_stack.ss_size = bytes;
    g_ctxs[id].ctx.uc_link = &g_sched_ctx;  /* return to scheduler if task exits */
    makecontext(&g_ctxs[id].ctx, hrt__task_trampoline, 0);
    g_ctxs[id].stk_ptr   = (void*)stack_base;
    g_ctxs[id].stk_bytes = bytes;
    g_ctxs[id].valid     = 1;
}

/* Tick handler: only set a flag; do not swap here */
static void _tick_sighandler(int signo) {
    (void)signo;
    hrt__tick_isr();
    g_switch_pending = 1;
}

/* Start periodic SIGALRM at the requested Hz */
void hrt_port_start_systick(uint32_t hz) {
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
    long usec = (hz ? (1000000L / (long)hz) : 1000L);
    it.it_value.tv_sec  = 0;
    it.it_value.tv_usec = usec;
    it.it_interval      = it.it_value;
    setitimer(ITIMER_REAL, &it, NULL);
}

/* Idle hook: tiny nap to avoid 100% CPU */
void hrt_port_idle_wait(void) {
    struct timespec ts = {0, 1*1000*1000}; /* 1 ms */
    nanosleep(&ts, NULL);
}

/* ISR-safe: just set a flag */
void hrt__pend_context_switch(void) {
    g_switch_pending = 1;
}

/* Task-context only: hop into the scheduler with SIGALRM masked */
void hrt_port_yield_to_scheduler(void){
    int cur = hrt__get_current();
    if (cur < 0 || !g_ctxs[cur].valid) return;
    sigset_t old;
    block_sigalrm(&old);
    swapcontext(&g_ctxs[cur].ctx, &g_sched_ctx);
    unblock_sigalrm(&old);
}

/* Scheduler loop: pick and switch, with SIGALRM masked during critical sections */
void hrt_port_enter_scheduler(void) {
    for(;;){
        if (!g_switch_pending) {
            hrt_port_idle_wait();
            continue;
        }
        g_switch_pending = 0;

        sigset_t old;
        block_sigalrm(&old);

        int next = hrt__pick_next_ready();
        if (next < 0) {
            unblock_sigalrm(&old);
            hrt_port_idle_wait();
            continue;
        }

        hrt__set_current(next);
        /* Jump from scheduler to task; task will swap back when it yields/sleeps */
        swapcontext(&g_sched_ctx, &g_ctxs[next].ctx);

        unblock_sigalrm(&old);
        /* when we return here, a yield/sleep/time-slice triggered it */
    }
}
