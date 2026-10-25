# Mutexes

HardRT provides an owner-tracked mutex for mutual exclusion in task context.

## Current semantics

The current `hrt_mutex_t` implementation is:

- owner-tracked;
- non-recursive;
- FIFO for waiters;
- direct-handoff on unlock;
- task-context-only.

Under `HRT_SCHED_PRIORITY` and `HRT_SCHED_PRIORITY_RR`, statically bounded **transitive priority inheritance** raises the effective priority of blocking owners. Global RR does not use priorities and offers no PI inversion bound. Recursive locking, ISR lock/unlock operations and automatic owner-death recovery remain unsupported. See [MUTEX_PRIORITY_INHERITANCE.md](MUTEX_PRIORITY_INHERITANCE.md).

## API

```c
#include "hardrt_mutex.h"

#define HRT_MUTEX_NO_OWNER (-1)

void hrt_mutex_init(hrt_mutex_t *mutex);
int  hrt_mutex_lock(hrt_mutex_t *mutex);
hrt_wait_result_t hrt_mutex_lock_until(hrt_mutex_t *mutex, hrt_tick_t deadline);
int  hrt_mutex_try_lock(hrt_mutex_t *mutex);
int  hrt_mutex_unlock(hrt_mutex_t *mutex);
```

## Initialization

```c
hrt_mutex_t mutex;
hrt_mutex_init(&mutex);
```

After initialization, the mutex is unlocked, its owner is `HRT_MUTEX_NO_OWNER`, and its waiter FIFO is empty.

## Try lock

`hrt_mutex_try_lock()` attempts to acquire the mutex without blocking.

- It returns `0` when the current task acquires ownership.
- It returns `-1` when the mutex is already locked or the current context is invalid.
- It returns `-1` when the current owner tries to acquire it again, because the mutex is non-recursive.

## Blocking lock

`hrt_mutex_lock()`:

- acquires an unlocked mutex and records the current task as owner;
- appends a contending task to the FIFO waiter queue and marks it blocked;
- rejects recursive acquisition by the current owner.

When a blocked task resumes after direct handoff, it already owns the mutex. The normal success result is `0`. The function returns `-1` for invalid task context, recursive acquisition, or if bounded waiter publication cannot be completed.

## Timed lock and priority inheritance

`hrt_mutex_lock_until()` uses an **absolute wrapping tick deadline** with `hrt_wait_result_t`. It always attempts an immediately available mutex once, even when the deadline is already due; otherwise an expired deadline returns `HRT_WAIT_TIMEOUT`. Blocking uses the shared finite-timeout engine, with waiter unlink and donation withdrawal serialized before READY publication.

`HARDRT_MAX_MUTEXES_HELD` (default 8, supported 1..32) bounds how many mutexes a task may own; acquisitions fail before mutation when the limit is exceeded. Owner donation propagates through at most the statically bounded task chain; cycles are rejected. A task's base and effective priorities differ while inheritance applies. Under global RR no PI bound is offered. These are **structural bounds**, not numerical WCET or application critical-section bounds. For a conservative finite **application-specific** blocking budget under explicit separately proven elapsed-time assumptions, see [MUTEX_BLOCKING_BUDGET.md](MUTEX_BLOCKING_BUDGET.md).

## Unlock

Only the owning task may unlock.

- With no waiter, the mutex becomes unlocked and the owner is reset.
- With a waiter, ownership is transferred directly to the first FIFO waiter and that task is made ready.
- A non-owner unlock returns `-1`.

Waiter selection is FIFO at the mutex, while final execution follows the scheduler's active policy.

## Task exit and mutex ownership

A task must release every mutex it owns before returning from its entry function or calling `hrt_task_delete()`.

The kernel records the task as `EXITED` and tracks a bounded list of owned mutexes for donation recalculation, **not** for automatic owner-death recovery. Mutexes are application-owned objects: the kernel does **not** automatically unlock, abandon, or transfer ownership when a task exits. If a task exits while owning a mutex, that mutex remains owned by a non-running task and later users can remain blocked indefinitely.

Automatic robust/owner-death mutex recovery is intentionally outside the 0.6 contract and requires a separate design rather than silently guessing whether protected state is still valid.

## C example

```c
#include "hardrt.h"

static hrt_mutex_t uart_lock;
static uint32_t telemetry_stack[1024];
static uint32_t log_stack[1024];

static void telemetry_task(void *arg) {
    (void)arg;
    for (;;) {
        hrt_mutex_lock(&uart_lock);
        /* Write telemetry frame. */
        hrt_mutex_unlock(&uart_lock);
        hrt_sleep(20);
    }
}

static void log_task(void *arg) {
    (void)arg;
    for (;;) {
        if (hrt_mutex_try_lock(&uart_lock) == 0) {
            /* Write log record. */
            hrt_mutex_unlock(&uart_lock);
        }
        hrt_sleep(5);
    }
}

int main(void) {
    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_SYSTICK
    };
    const hrt_task_attr_t attr = {
        .priority = HRT_PRIO1,
        .timeslice = 5
    };

    hrt_init(&cfg);
    hrt_mutex_init(&uart_lock);
    hrt_create_task(
        telemetry_task,
        NULL,
        telemetry_stack,
        1024,
        &attr);
    hrt_create_task(
        log_task,
        NULL,
        log_stack,
        1024,
        &attr);
    hrt_start();
}
```

## C++ example

```cpp
#include <hardrtpp.hpp>

static hardrt::Mutex uart_lock;

static void worker(void *arg) {
    (void)arg;
    for (;;) {
        uart_lock.lock();
        // Critical section.
        uart_lock.unlock();
        hardrt::Task::sleep(10);
    }
}
```

## Rules

- Call mutex APIs only from task context.
- Do not call mutex APIs from an ISR.
- Unlock only from the owning task.
- Do not attempt recursive locking.
- Release every owned mutex before task return or `hrt_task_delete()`.
- Analyze application mutex holding times and remaining blocking/interference; bounded PI mitigates scheduling inversion for fixed-priority modes but does not prove a numerical WCET or protect global RR.
