# Mutexes

HardRT v0.4.0 provides an owner-tracked mutex for mutual exclusion in task context.

## Current semantics

The current `hrt_mutex_t` implementation is:

- owner-tracked;
- non-recursive;
- FIFO for waiters;
- direct-handoff on unlock;
- task-context-only.

It does not provide priority inheritance, recursive locking, timeout operations, or ISR lock/unlock APIs.

## API

```c
#include "hardrt_mutex.h"

#define HRT_MUTEX_NO_OWNER (-1)

void hrt_mutex_init(hrt_mutex_t *mutex);
int  hrt_mutex_lock(hrt_mutex_t *mutex);
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

When a blocked task resumes after direct handoff, it already owns the mutex.

There is no timed-lock variant in v0.4.0.

## Unlock

Only the owning task may unlock.

- With no waiter, the mutex becomes unlocked and the owner is reset.
- With a waiter, ownership is transferred directly to the first FIFO waiter and that task is made ready.
- A non-owner unlock returns `-1`.

The current implementation yields after waking a waiter. Waiter selection is FIFO at the mutex, while final execution still follows the scheduler's priority-queue selection.

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
- Analyze priority inversion at the application level; v0.4.0 has no mitigation mechanism.
