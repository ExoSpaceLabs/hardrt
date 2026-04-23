# 🔐 Mutexes

HardRT provides a dedicated mutex primitive for mutual exclusion in task context.

## Current semantics

The current `hrt_mutex_t` implementation is:
- **owner-tracked**
- **non-recursive**
- **FIFO for waiters**
- **direct handoff on unlock**
- **task-context only**

The current implementation does **not** provide:
- priority inheritance
- recursive locking
- timed lock / timeout API
- ISR lock/unlock API

---

## API

```c
#include "hardrt_mutex.h"

#define HRT_MUTEX_NO_OWNER (-1)

void hrt_mutex_init(hrt_mutex_t *m);
int  hrt_mutex_lock(hrt_mutex_t *m);
int  hrt_mutex_try_lock(hrt_mutex_t *m);
int  hrt_mutex_unlock(hrt_mutex_t *m);
```

### Initialization

```c
hrt_mutex_t m;
hrt_mutex_init(&m);
```

After initialization:
- `locked == 0`
- `owner == HRT_MUTEX_NO_OWNER`
- waiter queue is empty

### `hrt_mutex_try_lock()`

Attempts to acquire the mutex without blocking.

Returns:
- `0` on success
- `-1` if already locked or the call is invalid for the current context

If the current owner calls `try_lock()` again, the call fails because the mutex is non-recursive.

### `hrt_mutex_lock()`

Blocks until the mutex becomes available.

Behavior:
- if unlocked, the current task becomes owner and returns `0`
- if locked by another task, the caller is queued FIFO and moved to `HRT_BLOCKED`
- if already owned by the caller, the call fails with `-1`

When a blocked task resumes after handoff, it already owns the mutex.

### `hrt_mutex_unlock()`

Releases a mutex owned by the current task.

Behavior:
- if there is no waiter, the mutex becomes unlocked and owner resets to `HRT_MUTEX_NO_OWNER`
- if there is a waiter, ownership is transferred directly to the next waiter and that task is made READY
- if called by a non-owner, the call fails with `-1`

---

## Scheduling behavior under contention

When a task unlocks a contested mutex, HardRT performs **direct handoff**:
1. dequeue one waiter
2. transfer ownership to that waiter
3. wake the waiter
4. yield so the awakened task can run promptly under scheduler policy

This avoids a release-then-race pattern and keeps handoff deterministic.

Waiter order is FIFO at the mutex queue level. Final execution order still respects the scheduler's global priority policy.

---

## Usage example (C)

```c
#include "hardrt.h"

static hrt_mutex_t uart_lock;

static void telemetry_task(void*) {
    for (;;) {
        hrt_mutex_lock(&uart_lock);
        /* write telemetry frame */
        hrt_mutex_unlock(&uart_lock);
        hrt_sleep(20);
    }
}

static void log_task(void*) {
    for (;;) {
        if (hrt_mutex_try_lock(&uart_lock) == 0) {
            /* write log record */
            hrt_mutex_unlock(&uart_lock);
        }
        hrt_sleep(5);
    }
}

int main(void) {
    static uint32_t s1[1024], s2[1024];
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_SYSTICK
    };

    hrt_init(&cfg);
    hrt_mutex_init(&uart_lock);

    hrt_task_attr_t p = { .priority = HRT_PRIO1, .timeslice = 5 };
    hrt_create_task(telemetry_task, 0, s1, 1024, &p);
    hrt_create_task(log_task, 0, s2, 1024, &p);
    hrt_start();
}
```

## Usage example (C++)

```cpp
#include <hardrtpp.hpp>

static hardrt::Mutex uart_lock;

static void task(void*) {
    for (;;) {
        uart_lock.lock();
        // critical section
        uart_lock.unlock();
        hardrt::Task::sleep(10);
    }
}
```

---

## Rules and caveats

- Call mutex APIs only from task context.
- Do not call mutex APIs from ISR.
- Unlock must be performed by the owner.
- Do not attempt recursive locking.
- Priority inversion mitigation is not yet implemented.

For cases where ownership does not matter and ISR interaction does, use a semaphore instead.
