/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hardrt.h"
#include "hardrt_time.h"
#include "hardrt_port_contract.h"

#define HRT_POSIX_PREEMPT_SIGNAL SIGALRM
#define HRT_POSIX_RESUME_SIGNAL  SIGUSR2
#define HRT_POSIX_HOST_STACK_MIN (256u * 1024u)

typedef enum {
    HRT_POSIX_PARK_NONE = 0,
    HRT_POSIX_PARK_GATE,
    HRT_POSIX_PARK_SIGNAL
} hrt_posix_park_t;

typedef struct {
    pthread_t thread;
    sem_t run_gate;
    void *stk_ptr;
    size_t stk_bytes;
    int thread_created;
    atomic_int alive;
    atomic_int park_kind;
    volatile sig_atomic_t resume_requested;
} _port_ctx_t;

static _port_ctx_t g_ctxs[HARDRT_MAX_TASKS];
static pthread_t g_scheduler_thread;
static pthread_t g_tick_thread;
static sem_t g_scheduler_gate;
static int g_scheduler_gate_initialized = 0;
static int g_tick_thread_created = 0;

static atomic_int g_scheduler_running = 0;
static atomic_int g_stop_requested = 0;
static atomic_int g_teardown = 0;
static atomic_int g_switch_pending = 0;
static atomic_int g_active_task = -1;
static atomic_uint g_tick_pending = 0u;
static atomic_flag g_kernel_lock = ATOMIC_FLAG_INIT;

static sigset_t g_preempt_set;
static sigset_t g_resume_wait_mask;
static uint64_t g_tick_period_ns = 0u;

static _Thread_local int g_tls_task_id = -1;
static _Thread_local int g_crit_depth = 0;
static _Thread_local sigset_t g_saved_mask;

#ifdef HARDRT_TEST_HOOKS
static atomic_ullong g_idle_counter = 0u;
static atomic_int g_test_fail_next_prepare = 0;
#endif

static void sem_wait_nointr(sem_t *sem) {
    while (sem_wait(sem) != 0 && errno == EINTR) {}
}

static void drain_sem(sem_t *sem) {
    while (sem_trywait(sem) == 0) {}
}

static int is_scheduler_thread(void) {
    return atomic_load_explicit(&g_scheduler_running, memory_order_acquire) != 0 &&
           pthread_equal(pthread_self(), g_scheduler_thread);
}

static int caller_is_active_task(void) {
    const int active = atomic_load_explicit(&g_active_task, memory_order_acquire);
    return g_tls_task_id >= 0 && g_tls_task_id == active;
}

static void request_preempt_current(void) {
    const int id = atomic_load_explicit(&g_active_task, memory_order_acquire);
    if (id < 0 || id >= HARDRT_APP_MAX_TASKS) {
        if (g_scheduler_gate_initialized) (void)sem_post(&g_scheduler_gate);
        return;
    }

    _port_ctx_t *ctx = &g_ctxs[id];
    if (!ctx->thread_created || atomic_load_explicit(&ctx->alive, memory_order_acquire) == 0) {
        if (g_scheduler_gate_initialized) (void)sem_post(&g_scheduler_gate);
        return;
    }

    if (pthread_kill(ctx->thread, HRT_POSIX_PREEMPT_SIGNAL) != 0) {
        if (g_scheduler_gate_initialized) (void)sem_post(&g_scheduler_gate);
    }
}

static void resume_signal_handler(const int signo) {
    (void)signo;
    const int id = g_tls_task_id;
    if (id < 0 || id >= HARDRT_APP_MAX_TASKS) return;
    g_ctxs[id].resume_requested = 1;
}

static void preempt_signal_handler(const int signo) {
    (void)signo;
    const int id = g_tls_task_id;
    if (id < 0 || id >= HARDRT_APP_MAX_TASKS) return;
    if (atomic_load_explicit(&g_active_task, memory_order_relaxed) != id) return;

    _port_ctx_t *ctx = &g_ctxs[id];
    ctx->resume_requested = 0;
    atomic_store_explicit(&ctx->park_kind, HRT_POSIX_PARK_SIGNAL, memory_order_release);
    atomic_store_explicit(&g_active_task, -1, memory_order_release);
    if (g_scheduler_gate_initialized) (void)sem_post(&g_scheduler_gate);

    while (ctx->resume_requested == 0 &&
           atomic_load_explicit(&g_teardown, memory_order_acquire) == 0) {
        (void)sigsuspend(&g_resume_wait_mask);
    }

    ctx->resume_requested = 0;
    atomic_store_explicit(&ctx->park_kind, HRT_POSIX_PARK_NONE, memory_order_release);
}

static void task_thread_cleanup(void *arg) {
    _port_ctx_t *ctx = (_port_ctx_t *)arg;
    atomic_store_explicit(&ctx->alive, 0, memory_order_release);
    atomic_store_explicit(&ctx->park_kind, HRT_POSIX_PARK_NONE, memory_order_release);
}

static void *task_thread_main(void *arg) {
    const int id = (int)(intptr_t)arg;
    _port_ctx_t *ctx = &g_ctxs[id];
    g_tls_task_id = id;

    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    (void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* Park newly-created tasks until the HardRT scheduler selects them. */
    (void)pthread_sigmask(SIG_BLOCK, &g_preempt_set, NULL);
    atomic_store_explicit(&ctx->park_kind, HRT_POSIX_PARK_GATE, memory_order_release);

    pthread_cleanup_push(task_thread_cleanup, ctx);
    sem_wait_nointr(&ctx->run_gate);
    if (atomic_load_explicit(&g_teardown, memory_order_acquire) == 0) {
        atomic_store_explicit(&ctx->park_kind, HRT_POSIX_PARK_NONE, memory_order_release);
        (void)pthread_sigmask(SIG_UNBLOCK, &g_preempt_set, NULL);
        hrt__task_trampoline();
    }
    pthread_cleanup_pop(1);
    return NULL;
}

static void stop_tick_thread(void) {
    if (!g_tick_thread_created) return;
    atomic_store_explicit(&g_scheduler_running, 0, memory_order_release);
    (void)pthread_kill(g_tick_thread, HRT_POSIX_RESUME_SIGNAL);
    (void)pthread_join(g_tick_thread, NULL);
    g_tick_thread_created = 0;
}

static void cleanup_task_threads(void) {
    atomic_store_explicit(&g_teardown, 1, memory_order_release);

    for (int i = 0; i < HARDRT_APP_MAX_TASKS; ++i) {
        _port_ctx_t *ctx = &g_ctxs[i];
        if (!ctx->thread_created) continue;

        (void)sem_post(&ctx->run_gate);
        if (atomic_load_explicit(&ctx->alive, memory_order_acquire) != 0) {
            (void)pthread_kill(ctx->thread, HRT_POSIX_RESUME_SIGNAL);
            (void)pthread_cancel(ctx->thread);
        }
        (void)pthread_join(ctx->thread, NULL);
        (void)sem_destroy(&ctx->run_gate);
        memset(ctx, 0, sizeof(*ctx));
    }

    atomic_store_explicit(&g_teardown, 0, memory_order_release);
}

#ifdef HARDRT_TEST_HOOKS
void hrt__test_stop_scheduler(void) {
    atomic_store_explicit(&g_stop_requested, 1, memory_order_release);
    if (caller_is_active_task()) return;
    request_preempt_current();
}

void hrt__test_reset_scheduler_state(void) {
    stop_tick_thread();
    cleanup_task_threads();

    if (g_scheduler_gate_initialized) drain_sem(&g_scheduler_gate);
    atomic_store_explicit(&g_stop_requested, 0, memory_order_release);
    atomic_store_explicit(&g_switch_pending, 1, memory_order_release);
    atomic_store_explicit(&g_active_task, -1, memory_order_release);
    atomic_store_explicit(&g_tick_pending, 0u, memory_order_release);
    atomic_store_explicit(&g_idle_counter, 0u, memory_order_release);
    atomic_store_explicit(&g_test_fail_next_prepare, 0, memory_order_release);
    atomic_flag_clear_explicit(&g_kernel_lock, memory_order_release);
    g_crit_depth = 0;
    hrt__test_reset_kernel_state();
}

void hrt__test_idle_counter_reset(void) {
    atomic_store_explicit(&g_idle_counter, 0u, memory_order_release);
}

unsigned long long hrt__test_idle_counter_value(void) {
    return atomic_load_explicit(&g_idle_counter, memory_order_acquire);
}

void hrt__test_fail_next_prepare_task_stack(void) {
    atomic_store_explicit(&g_test_fail_next_prepare, 1, memory_order_release);
}

void hrt__test_fast_forward_ticks(uint32_t delta) {
    sigset_t old;
    (void)pthread_sigmask(SIG_BLOCK, &g_preempt_set, &old);
    for (uint32_t i = 0; i < delta; ++i) hrt__tick_isr();
    (void)pthread_sigmask(SIG_SETMASK, &old, NULL);
}

void hrt__test_block_sigalrm(void) {
    (void)pthread_sigmask(SIG_BLOCK, &g_preempt_set, NULL);
}

void hrt__test_unblock_sigalrm(void) {
    (void)pthread_sigmask(SIG_UNBLOCK, &g_preempt_set, NULL);
}
#endif

void hrt__task_trampoline(void) {
    const int id = hrt__get_current();
    const _hrt_tcb_t *t = hrt__tcb(id);
    t->entry(t->arg);
    hrt_task_delete();
}

int hrt_port_prepare_task_stack(const int id, void (*tramp)(void),
                                uint32_t *stack_base, const size_t words) {
    (void)tramp;
    if (id < 0 || id >= HARDRT_APP_MAX_TASKS || stack_base == NULL || words == 0u) return -1;

#ifdef HARDRT_TEST_HOOKS
    if (atomic_exchange_explicit(&g_test_fail_next_prepare, 0, memory_order_acq_rel) != 0) {
        return -1;
    }
#endif

    _port_ctx_t *ctx = &g_ctxs[id];
    if (ctx->thread_created) {
        if (atomic_load_explicit(&ctx->alive, memory_order_acquire) != 0) return -1;
        (void)pthread_join(ctx->thread, NULL);
        (void)sem_destroy(&ctx->run_gate);
        memset(ctx, 0, sizeof(*ctx));
    }

    if (sem_init(&ctx->run_gate, 0, 0) != 0) return -1;
    ctx->stk_ptr = (void *)stack_base;
    ctx->stk_bytes = words * sizeof(uint32_t);
    ctx->resume_requested = 0;
    atomic_init(&ctx->alive, 0);
    atomic_init(&ctx->park_kind, HRT_POSIX_PARK_GATE);

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        (void)sem_destroy(&ctx->run_gate);
        memset(ctx, 0, sizeof(*ctx));
        return -1;
    }

    size_t host_stack = ctx->stk_bytes;
    if (host_stack < HRT_POSIX_HOST_STACK_MIN) host_stack = HRT_POSIX_HOST_STACK_MIN;
    if (pthread_attr_setstacksize(&attr, host_stack) != 0) {
        (void)pthread_attr_destroy(&attr);
        (void)sem_destroy(&ctx->run_gate);
        memset(ctx, 0, sizeof(*ctx));
        return -1;
    }

    const int rc = pthread_create(&ctx->thread, &attr, task_thread_main, (void *)(intptr_t)id);
    (void)pthread_attr_destroy(&attr);
    if (rc != 0) {
        (void)sem_destroy(&ctx->run_gate);
        memset(ctx, 0, sizeof(*ctx));
        return -1;
    }

    ctx->thread_created = 1;
    atomic_store_explicit(&ctx->alive, 1, memory_order_release);
    return 0;
}

static void *tick_thread_main(void *arg) {
    (void)arg;

    struct timespec next;
    (void)clock_gettime(CLOCK_MONOTONIC, &next);

    while (atomic_load_explicit(&g_scheduler_running, memory_order_acquire) != 0) {
        next.tv_nsec += (long)(g_tick_period_ns % 1000000000ULL);
        next.tv_sec += (time_t)(g_tick_period_ns / 1000000000ULL);
        if (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec += 1;
        }

        int rc;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        } while (rc == EINTR && atomic_load_explicit(&g_scheduler_running, memory_order_acquire) != 0);
        if (atomic_load_explicit(&g_scheduler_running, memory_order_acquire) == 0) break;

        (void)atomic_fetch_add_explicit(&g_tick_pending, 1u, memory_order_acq_rel);
        request_preempt_current();
    }
    return NULL;
}

int hrt_port_configure_tick(const uint32_t tick_hz) {
    sigemptyset(&g_preempt_set);
    sigaddset(&g_preempt_set, HRT_POSIX_PREEMPT_SIGNAL);
    sigfillset(&g_resume_wait_mask);
    sigdelset(&g_resume_wait_mask, HRT_POSIX_RESUME_SIGNAL);

    struct sigaction preempt_action;
    memset(&preempt_action, 0, sizeof(preempt_action));
    preempt_action.sa_handler = preempt_signal_handler;
    sigemptyset(&preempt_action.sa_mask);
    sigaddset(&preempt_action.sa_mask, HRT_POSIX_RESUME_SIGNAL);
    preempt_action.sa_flags = SA_RESTART;
    if (sigaction(HRT_POSIX_PREEMPT_SIGNAL, &preempt_action, NULL) != 0) return -1;

    struct sigaction resume_action;
    memset(&resume_action, 0, sizeof(resume_action));
    resume_action.sa_handler = resume_signal_handler;
    sigemptyset(&resume_action.sa_mask);
    resume_action.sa_flags = SA_RESTART;
    if (sigaction(HRT_POSIX_RESUME_SIGNAL, &resume_action, NULL) != 0) return -1;

    if (!g_scheduler_gate_initialized) {
        if (sem_init(&g_scheduler_gate, 0, 0) != 0) return -1;
        g_scheduler_gate_initialized = 1;
    } else {
        drain_sem(&g_scheduler_gate);
    }

    atomic_store_explicit(&g_stop_requested, 0, memory_order_release);
    atomic_store_explicit(&g_switch_pending, 1, memory_order_release);
    atomic_store_explicit(&g_active_task, -1, memory_order_release);
    atomic_store_explicit(&g_tick_pending, 0u, memory_order_release);
    atomic_flag_clear_explicit(&g_kernel_lock, memory_order_release);

    if (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL) {
        g_tick_period_ns = 0u;
        return 0;
    }
    if (tick_hz == 0u) return -1;

    g_tick_period_ns = 1000000000ULL / (uint64_t)tick_hz;
    if (g_tick_period_ns == 0u) return -1;
    return 0;
}

void hrt_port_idle_wait(void) {
#ifdef HARDRT_TEST_HOOKS
    (void)atomic_fetch_add_explicit(&g_idle_counter, 1u, memory_order_relaxed);
#endif
    const struct timespec ts = {0, 1000 * 1000};
    (void)nanosleep(&ts, NULL);
}

void hrt__pend_context_switch(void) {
    atomic_store_explicit(&g_switch_pending, 1, memory_order_release);

    if (atomic_load_explicit(&g_scheduler_running, memory_order_acquire) == 0) return;
    if (is_scheduler_thread()) return;
    if (caller_is_active_task()) return;
    request_preempt_current();
}

void hrt_port_yield_to_scheduler(void) {
    int cur = g_tls_task_id;
    if (cur < 0) cur = hrt__get_current();
    if (cur < 0 || cur >= HARDRT_APP_MAX_TASKS || !g_ctxs[cur].thread_created) return;

    _port_ctx_t *ctx = &g_ctxs[cur];
    sigset_t old;
    (void)pthread_sigmask(SIG_BLOCK, &g_preempt_set, &old);

    atomic_store_explicit(&ctx->park_kind, HRT_POSIX_PARK_GATE, memory_order_release);
    atomic_store_explicit(&g_active_task, -1, memory_order_release);
    if (g_scheduler_gate_initialized) (void)sem_post(&g_scheduler_gate);

    const _hrt_tcb_t *t = hrt__tcb(cur);
    if (t != NULL && t->state == HRT_EXITED) {
        pthread_exit(NULL);
    }

    sem_wait_nointr(&ctx->run_gate);
    atomic_store_explicit(&ctx->park_kind, HRT_POSIX_PARK_NONE, memory_order_release);
    (void)pthread_sigmask(SIG_SETMASK, &old, NULL);
}

static void resume_task(const int id) {
    _port_ctx_t *ctx = &g_ctxs[id];
    if (!ctx->thread_created || atomic_load_explicit(&ctx->alive, memory_order_acquire) == 0) return;

    const int park = atomic_load_explicit(&ctx->park_kind, memory_order_acquire);
    atomic_store_explicit(&g_active_task, id, memory_order_release);

    if (park == HRT_POSIX_PARK_SIGNAL) {
        (void)pthread_kill(ctx->thread, HRT_POSIX_RESUME_SIGNAL);
    } else {
        (void)sem_post(&ctx->run_gate);
    }
}

void hrt_port_enter_scheduler(void) {
    g_scheduler_thread = pthread_self();
    atomic_store_explicit(&g_scheduler_running, 1, memory_order_release);
    atomic_store_explicit(&g_stop_requested, 0, memory_order_release);
    atomic_store_explicit(&g_switch_pending, 1, memory_order_release);
    atomic_store_explicit(&g_active_task, -1, memory_order_release);

    /* The scheduler/controller thread must never receive the task-preemption signal. */
    (void)pthread_sigmask(SIG_BLOCK, &g_preempt_set, NULL);

    int tick_started = (hrt__cfg_tick_src() == HRT_TICK_EXTERNAL);
    int schedule_needed = 1;

    for (;;) {
#ifdef HARDRT_TEST_HOOKS
        if (atomic_load_explicit(&g_stop_requested, memory_order_acquire) != 0) break;
#endif

        if (schedule_needed) {
            hrt_port_crit_enter();
            hrt__on_scheduler_entry();
            const int next = hrt__pick_next_ready();
            if (next >= 0 && next != HRT_IDLE_ID) {
                hrt__set_current(next);
            }
            atomic_store_explicit(&g_switch_pending, 0, memory_order_release);
            hrt_port_crit_exit();

            if (next >= 0 && next != HRT_IDLE_ID) {
                if (!tick_started) {
                    if (pthread_create(&g_tick_thread, NULL, tick_thread_main, NULL) != 0) {
                        atomic_store_explicit(&g_scheduler_running, 0, memory_order_release);
                        return;
                    }
                    g_tick_thread_created = 1;
                    tick_started = 1;
                }
                resume_task(next);
            }
        } else {
            const int cur = hrt__get_current();
            if (cur >= 0 && cur < HARDRT_APP_MAX_TASKS) resume_task(cur);
        }

        if (atomic_load_explicit(&g_active_task, memory_order_acquire) < 0) {
            hrt_port_idle_wait();
            (void)sem_trywait(&g_scheduler_gate);
        } else {
            sem_wait_nointr(&g_scheduler_gate);
        }

#ifdef HARDRT_TEST_HOOKS
        if (atomic_load_explicit(&g_stop_requested, memory_order_acquire) != 0) break;
#endif

        hrt_port_crit_enter();
        const unsigned pending_ticks = atomic_exchange_explicit(&g_tick_pending, 0u, memory_order_acq_rel);
        for (unsigned i = 0u; i < pending_ticks; ++i) hrt__tick_isr();
        schedule_needed = atomic_exchange_explicit(&g_switch_pending, 0, memory_order_acq_rel) != 0;
        hrt_port_crit_exit();
    }

    atomic_store_explicit(&g_active_task, -1, memory_order_release);
    stop_tick_thread();
}

void hrt_port_crit_enter(void) {
    if (g_crit_depth++ != 0) return;

    (void)pthread_sigmask(SIG_BLOCK, &g_preempt_set, &g_saved_mask);
    while (atomic_flag_test_and_set_explicit(&g_kernel_lock, memory_order_acquire)) {
        sched_yield();
    }
}

void hrt_port_crit_exit(void) {
    if (g_crit_depth <= 0) {
        g_crit_depth = 0;
        return;
    }
    if (--g_crit_depth != 0) return;

    atomic_flag_clear_explicit(&g_kernel_lock, memory_order_release);
    (void)pthread_sigmask(SIG_SETMASK, &g_saved_mask, NULL);
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
