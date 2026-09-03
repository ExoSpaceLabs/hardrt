# C API Overview

This page summarizes the public C surface used by HardRT. Versioned notes identify behavior shipped in v0.4.0; scheduler and wake semantics described as current `develop` behavior are the implemented v0.5-line contract.

## Core types

```c
typedef void (*hrt_task_fn)(void *arg);

typedef enum {
    HRT_READY = 0,
    HRT_SLEEP,
    HRT_BLOCKED,
    HRT_UNUSED
} hrt_state_t;

typedef enum {
    HRT_SCHED_PRIORITY = 0,
    HRT_SCHED_RR,
    HRT_SCHED_PRIORITY_RR
} hrt_policy_t;

typedef enum {
    HRT_TICK_SYSTICK = 0,
    HRT_TICK_EXTERNAL = 1
} hrt_tick_source_t;

typedef struct {
    uint32_t          tick_hz;
    hrt_policy_t      policy;
    uint16_t          default_slice;
    uint32_t          core_hz;
    hrt_tick_source_t tick_src;
} hrt_config_t;

typedef struct {
    hrt_prio_t priority;
    uint16_t   timeslice;
} hrt_task_attr_t;
```

Priority `0` is the highest priority. The usable range is limited by `HARDRT_MAX_PRIO`, even though symbolic values through `HRT_PRIO11` are declared.

## Version and port identity

```c
const char *hrt_version_string(void);
unsigned    hrt_version_u32(void);
const char *hrt_port_name(void);
int         hrt_port_id(void);
```

For v0.4.0, `hrt_version_u32()` encodes the version as `(major << 16) | (minor << 8) | patch`.

## Kernel lifecycle

```c
int  hrt_init(const hrt_config_t *cfg);
int  hrt_create_task(hrt_task_fn fn,
                     void *arg,
                     uint32_t *stack_words,
                     size_t n_words,
                     const hrt_task_attr_t *attr);
void hrt_start(void);
```

### `hrt_init`

`hrt_init(NULL)` selects these defaults:

- tick frequency: `1000 Hz`;
- policy: `HRT_SCHED_PRIORITY_RR`;
- default slice: `5` ticks;
- tick source: `HRT_TICK_SYSTICK`.

With an explicit configuration:

- `tick_hz == 0` becomes `1000`;
- `default_slice == 0` becomes `5`;
- `policy` and `tick_src` are stored without validation;
- the port tick-start hook is called during initialization.

The current implementation always returns `0`. It does not reject repeated initialization or invalid lifecycle ordering. Applications should call it once before creating tasks or starting the scheduler.

### `hrt_create_task`

The function returns a non-negative task ID on success and `-1` on the checked failure paths.

Current requirements:

- `fn` is non-null;
- `stack_words` is non-null;
- `n_words >= 64`;
- a task slot is available.

If `attr == NULL`, the task uses priority `HRT_PRIO1` and the current default slice. If an attribute object is supplied, its `timeslice` is used exactly; `timeslice == 0` makes that task cooperative.

The CMake build reserves one additional slot for the idle task by defining `HARDRT_MAX_TASKS` as `HARDRT_CFG_MAX_TASKS + 1`.

### `hrt_start`

The function requests initial scheduling and enters the selected port scheduler.

- Cortex-M does not return under normal operation.
- POSIX returns only through test hooks in the test build.
- The null port returns without running tasks.

## Scheduling policies

Ready tasks are currently stored in one FIFO queue per priority. Priority-based policies always select from the highest non-empty priority class.

- `HRT_SCHED_PRIORITY` uses strict fixed-priority selection without tick-driven slice accounting. A strictly higher-priority wake requests preemption; equal- and lower-priority wakes do not force a switch merely because they became READY.
- `HRT_SCHED_PRIORITY_RR` uses the same priority dominance and rotates tasks only within the selected priority class. Higher-priority preemption preserves the interrupted task's queue precedence and unused quantum; explicit yield or quantum expiry rotates it to the tail exactly once and refreshes the next quantum.
- `HRT_SCHED_RR` still uses the transitional per-priority representation on `develop`; the final global priority-independent RR contract is tracked by #28.
- `timeslice == 0` disables tick-driven rotation for that task.

A timeslice is measured in ticks. See [SCHEDULING.md](SCHEDULING.md) for the complete READY-transition, scheduler-entry, and `need_switch` contract.

## Task control and time

```c
void     hrt_sleep(uint32_t ms);
void     hrt_yield(void);
void     hrt_task_delete(void);
uint32_t hrt_tick_now(void);
uint32_t hrt_now_ms(void);
```

### `hrt_sleep`

Milliseconds are converted with ceiling division:

```text
ceil(ms * tick_hz / 1000)
```

Positive durations shorter than one tick sleep for one tick. In v0.4.0 and the current implementation, `hrt_sleep(0)` also sleeps for one tick rather than behaving like `hrt_yield()`.

### `hrt_yield`

The current READY task is rotated to the tail of its priority queue exactly once, its configured slice is refreshed, rescheduling is requested, and task context is transferred to the port scheduler.

### `hrt_task_delete`

The current non-idle task is marked `HRT_UNUSED` and yields to the scheduler. Both reference task trampolines call this function when a task entry returns.

### Time queries

`hrt_tick_now()` returns the wrapping 32-bit tick count. `hrt_now_ms()` computes `(ticks * 1000) / tick_hz` using a 64-bit intermediate.

## Runtime tuning

```c
void hrt_set_policy(hrt_policy_t policy);
void hrt_set_default_timeslice(uint16_t ticks);
```

`hrt_set_policy()` stores the new value without validation or rebuilding ready queues. The next scheduling decision uses the new value.

`hrt_set_default_timeslice()` affects tasks created later with `attr == NULL`. Existing task configurations are unchanged. Setting the default to zero makes later default-created tasks cooperative.

## Tick API

```c
void hrt_tick_from_isr(void);
```

This public function is only for `HRT_TICK_EXTERNAL`. It advances time through the core tick handler and internally requests rescheduling when a sleeper wake or slice expiry requires it.

When the selected source is `HRT_TICK_SYSTICK`, the current implementation ignores calls to `hrt_tick_from_isr()`. Port-owned tick handlers call the private `hrt__tick_isr()` path instead.

## Semaphores

```c
void hrt_sem_init(hrt_sem_t *sem, unsigned init);
void hrt_sem_init_counting(hrt_sem_t *sem,
                           unsigned init,
                           uint8_t max_count);
int  hrt_sem_take(hrt_sem_t *sem);
int  hrt_sem_try_take(hrt_sem_t *sem);
int  hrt_sem_give(hrt_sem_t *sem);
int  hrt_sem_give_from_isr(hrt_sem_t *sem, int *need_switch);
```

Current `develop` behavior:

- binary semaphores saturate at `1`;
- counting semaphores saturate at `max_count`;
- `max_count == 0` is changed to `1`;
- waiters are stored FIFO;
- a give with a waiter performs direct handoff and wakes exactly one task;
- task-context give requests preemption only when the awakened waiter should run before the current task under the active scheduler contract;
- that preemption is not treated as explicit yield, so an interrupted PRIORITY_RR task retains precedence and unused quantum;
- ISR give sets `need_switch` to the same scheduler-aware decision and requests the switch internally when required.

For priority-based policies, `need_switch == 1` means the awakened waiter has strictly higher priority than the current READY task, or there is no normal READY current task. It does not merely mean that some waiter was awakened.

See [SEMAPHORES.md](SEMAPHORES.md).

## Mutexes

```c
void hrt_mutex_init(hrt_mutex_t *mutex);
int  hrt_mutex_lock(hrt_mutex_t *mutex);
int  hrt_mutex_try_lock(hrt_mutex_t *mutex);
int  hrt_mutex_unlock(hrt_mutex_t *mutex);
```

Mutexes are owner-tracked, non-recursive, task-context-only, and use FIFO waiters with direct handoff. Unlock uses the same scheduler-aware wake/preemption rule as other IPC paths. There is no timed lock, recursive mode, ISR API, or priority inheritance.

See [MUTEXES.md](MUTEXES.md).

## Message queues

```c
void     hrt_queue_init(hrt_queue_t *queue,
                        void *storage,
                        uint16_t capacity,
                        size_t item_size);
int      hrt_queue_send(hrt_queue_t *queue, const void *item);
int      hrt_queue_try_send(hrt_queue_t *queue, const void *item);
int      hrt_queue_try_send_from_isr(hrt_queue_t *queue,
                                     const void *item,
                                     int *need_switch);
int      hrt_queue_recv(hrt_queue_t *queue, void *out);
int      hrt_queue_try_recv(hrt_queue_t *queue, void *out);
int      hrt_queue_try_recv_from_isr(hrt_queue_t *queue,
                                     void *out,
                                     int *need_switch);
uint16_t hrt_queue_count(const hrt_queue_t *queue);
```

Items are copied with `memcpy` while the port critical section is held. Blocking operations wait indefinitely. Task and ISR wake paths use the common scheduler-aware preemption rule. ISR variants are non-blocking, expose that decision through `need_switch`, and request rescheduling internally when required.

See [QUEUES.md](QUEUES.md).

## Minimal example

```c
#include "hardrt.h"

static uint32_t stack_a[2048];
static uint32_t stack_b[2048];

static void task_a(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sleep(500);
    }
}

static void task_b(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sleep(1000);
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

    const hrt_task_attr_t a = {
        .priority = HRT_PRIO0,
        .timeslice = 0
    };
    const hrt_task_attr_t b = {
        .priority = HRT_PRIO1,
        .timeslice = 5
    };

    hrt_init(&cfg);
    hrt_create_task(task_a, NULL, stack_a, 2048, &a);
    hrt_create_task(task_b, NULL, stack_b, 2048, &b);
    hrt_start();
}
```