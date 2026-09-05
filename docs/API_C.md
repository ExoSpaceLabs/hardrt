# C API Overview

This page summarizes the public C surface used by HardRT. Versioned notes identify behavior shipped in v0.4.0; scheduler and wake semantics described as current `develop` behavior are the implemented v0.5-line contract.

## Core types

```c
typedef void (*hrt_task_fn)(void *arg);

typedef enum {
    HRT_OK = 0,
    HRT_ERR_ALREADY_INITIALIZED = -1,
    HRT_ERR_INVALID_CONFIG = -2,
    HRT_ERR_INVALID_STATE = -3,
    HRT_ERR_PORT_INIT = -4
} hrt_status_t;

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

Task execution state and TCB-slot ownership are kernel-private implementation details, not public C types. The v0.5 kernel deliberately keeps them separate:

- slot ownership is `UNUSED` or `USED`;
- a used task is `READY`, `RUNNING`, `SLEEP`, `BLOCKED`, or `EXITED`;
- `READY` means present in the runnable scheduler representation;
- `RUNNING` means currently dispatched;
- `EXITED` remains a valid task state while its slot is still occupied, until a later task creation reclaims that slot.

This separation avoids treating allocation status as an execution state.

## Version and port identity

```c
const char *hrt_version_string(void);
unsigned    hrt_version_u32(void);
const char *hrt_port_name(void);
int         hrt_port_id(void);
```

`hrt_version_u32()` encodes the version as `(major << 16) | (minor << 8) | patch`.

## Kernel lifecycle

```c
hrt_status_t hrt_init(const hrt_config_t *cfg);
int          hrt_create_task(hrt_task_fn fn,
                             void *arg,
                             uint32_t *stack_words,
                             size_t n_words,
                             const hrt_task_attr_t *attr);
hrt_status_t hrt_start(void);
```

The public lifecycle is explicit:

```text
UNINITIALIZED -> INITIALIZED -> RUNNING
```

`hrt_init()` is the only public transition from `UNINITIALIZED` to `INITIALIZED`. `hrt_start()` is the only public transition from `INITIALIZED` to `RUNNING`. A second initialization or scheduler start is rejected instead of silently resetting kernel state. Test-only lifecycle reset is compiled only with `HARDRT_TEST_HOOKS`.

### `hrt_init`

`hrt_init(NULL)` selects these defaults:

- tick frequency: `1000 Hz`;
- policy: `HRT_SCHED_PRIORITY_RR`;
- default slice: `5` ticks;
- `core_hz = 0`;
- tick source: `HRT_TICK_SYSTICK`.

With an explicit configuration:

- `tick_hz` must be in the public non-zero `uint32_t` range `[HRT_TICK_HZ_MIN, HRT_TICK_HZ_MAX]`, currently `1 .. UINT32_MAX`;
- policy must be one of the declared `HRT_SCHED_*` values;
- tick source must be `HRT_TICK_SYSTICK` or `HRT_TICK_EXTERNAL`;
- `default_slice == 0` is preserved and means cooperative scheduling for subsequently default-created tasks;
- `core_hz` is only valid for the Cortex-M port with `HRT_TICK_SYSTICK`;
- on Cortex-M/SysTick, `core_hz == 0` delegates clock discovery to `hrt_port_get_core_hz()`, while a non-zero value explicitly overrides that clock for SysTick reload calculation;
- `core_hz` must be zero with `HRT_TICK_EXTERNAL` and on ports that do not consume a CPU core clock.

For a port-owned tick source, the port may impose a narrower representable frequency range. For example, Cortex-M requires a SysTick reload that fits the 24-bit counter and POSIX requires a non-zero microsecond timer period. An otherwise valid request that the selected port cannot represent returns `HRT_ERR_PORT_INIT`. Initialization failure leaves the lifecycle `UNINITIALIZED`, so the caller may retry with a corrected configuration.

Scheduler structures and the private idle task are initialized before tick configuration. Tick configuration is prepared during initialization, but the configured periodic tick is not activated until scheduler start.

Public return values are:

- `HRT_OK` on success;
- `HRT_ERR_ALREADY_INITIALIZED` on a second `hrt_init()`;
- `HRT_ERR_INVALID_CONFIG` for invalid policy/source/frequency or contradictory unsupported configuration;
- `HRT_ERR_PORT_INIT` when the selected port cannot configure the requested tick.

These application-facing statuses are separate from `hrt_err`, which remains the lower-level diagnostic channel for kernel/API diagnostics.

### `hrt_create_task`

The function returns a non-negative task ID on success and `-1` on checked failure paths. Calling it before successful initialization is rejected. Task creation remains supported after `hrt_start()`.

Current requirements include:

- `fn` is non-null;
- `stack_words` is non-null;
- `n_words >= 64`;
- priority is within the configured priority range;
- the supplied stack range does not overlap the stack of any live task;
- a free or reclaimable application TCB slot is available.

If `attr == NULL`, the task uses the configured default slice and a valid default priority. If an attribute object is supplied, its `timeslice` is used exactly; `timeslice == 0` makes that task cooperative.

Task creation is transactional. A newly allocated slot is not published as USED/READY until port context preparation succeeds. An EXITED task no longer executes and its slot may later be reclaimed. Reusing the exact stack of an EXITED task preferentially reclaims that task's slot. A live task's stack may never be reused or partially overlapped by another live task.

When creation occurs while the scheduler is RUNNING, allocation/context preparation and READY publication are serialized under the kernel critical section. Successful runtime creation does not itself force immediate preemption; the new READY task participates at the next scheduling point.

The CMake build reserves one additional slot for the idle task by defining total task storage as `HARDRT_APP_MAX_TASKS + 1`.

### `hrt_start`

`hrt_start()` is valid only from `INITIALIZED`. Calls before initialization or after scheduler start return `HRT_ERR_INVALID_STATE`.

On a successful start the lifecycle becomes `RUNNING` before control crosses into the port scheduler:

- Cortex-M does not return under normal operation;
- POSIX returns only through test hooks in the test build;
- the null port returns without running tasks.

If a successful scheduler entry returns on a hosted/test port, `hrt_start()` returns `HRT_OK`; the lifecycle remains `RUNNING`, so a second start is still invalid.

## Scheduling policies

Ready tasks use a policy-specific intrusive FIFO representation. `HRT_SCHED_PRIORITY` and `HRT_SCHED_PRIORITY_RR` use one FIFO per priority plus the non-empty priority bitmap. `HRT_SCHED_RR` uses one global FIFO and ignores priority.

- `HRT_SCHED_PRIORITY` uses strict fixed-priority selection without tick-driven slice accounting. A strictly higher-priority wake requests preemption; equal- and lower-priority wakes do not force a switch merely because they became READY.
- `HRT_SCHED_PRIORITY_RR` uses the same priority dominance and rotates tasks only within the selected priority class. Higher-priority preemption preserves the interrupted task's queue precedence and unused quantum; explicit yield or quantum expiry rotates it to the tail exactly once and refreshes the next quantum.
- `HRT_SCHED_RR` is true global round-robin: every READY application task participates in one FIFO regardless of priority value. A wake joins the global tail and does not steal the RUNNING task's remaining quantum. Explicit yield or quantum expiry rotates to the global tail.
- `timeslice == 0` disables tick-driven rotation for that task under either RR-capable policy.

A timeslice is measured in ticks. See [SCHEDULING.md](SCHEDULING.md) for the complete READY transition, runtime policy-switching, scheduler-entry, and `need_switch` contract.

## Task control and time

```c
void     hrt_sleep(uint32_t ms);
void     hrt_yield(void);
void     hrt_task_delete(void);
uint32_t hrt_tick_now(void);
uint32_t hrt_now_ms(void);
```

### `hrt_sleep`

For positive durations, milliseconds are converted with ceiling division:

```text
ceil(ms * tick_hz / 1000)
```

Positive durations shorter than one tick therefore sleep for one tick. `hrt_sleep(0)` is different: it performs an immediate scheduling point with the same rotation semantics as `hrt_yield()`, does not enter `SLEEP`, does not join the sleep queue, and does not require a tick before the caller can run again.

### `hrt_yield`

The current RUNNING task reaches a scheduling point. It is returned to READY state and reinserted according to the active scheduler policy. Explicit yield rotates it to the appropriate tail and refreshes its configured quantum.

### `hrt_task_delete`

The current non-idle RUNNING task enters `EXITED` and yields to the scheduler. Its TCB slot remains USED until a later creation reclaims it. Both reference task trampolines call this function automatically when a task entry returns.

### Time queries

`hrt_tick_now()` returns the wrapping 32-bit tick count. `hrt_now_ms()` computes `(ticks * 1000) / tick_hz` using a 64-bit intermediate.

## Runtime tuning

```c
void hrt_set_policy(hrt_policy_t policy);
void hrt_set_default_timeslice(uint16_t ticks);
```

`hrt_set_policy()` supports runtime switching among `HRT_SCHED_PRIORITY`, `HRT_SCHED_RR`, and `HRT_SCHED_PRIORITY_RR`. If the requested policy differs from the active one, queued READY tasks are snapshotted and rebuilt under the target representation while the kernel critical section is held. READY-task quanta are refreshed, and a RUNNING application task treats the change as a scheduling point and rejoins the target policy at its tail. Selecting the already-active policy is a no-op. Policy switching is task-context-only.

For priority-to-global conversion, queued tasks are flattened from highest to lowest priority while preserving FIFO order inside each class. For global-to-priority conversion, global FIFO order is preserved within each resulting priority class.

`hrt_set_default_timeslice()` affects tasks created later with `attr == NULL`. Existing task configurations are unchanged. Setting the default to zero makes later default-created tasks cooperative.

## Tick API

```c
void hrt_tick_from_isr(void);
```

This public function is only for `HRT_TICK_EXTERNAL`. It advances time through the core tick handler and internally requests rescheduling when a sleeper wake or slice expiry requires it.

When the selected source is `HRT_TICK_SYSTICK`, calling `hrt_tick_from_isr()` is diagnosed as a tick-source mismatch and does not advance time. Port-owned tick handlers call the private `hrt__tick_isr()` path instead.

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

Current behavior:

- binary semaphores saturate at `1`;
- counting semaphores saturate at `max_count`;
- `max_count == 0` is changed to `1`;
- waiters are stored FIFO;
- task-context give requests preemption only when the awakened waiter should run before the current RUNNING task under the active scheduler contract;
- that preemption is not treated as explicit yield, so an interrupted PRIORITY_RR task retains precedence and unused quantum;
- ISR give exposes the same scheduler-aware decision through `need_switch` and requests the switch internally when required.

See [SEMAPHORES.md](SEMAPHORES.md).

## Mutexes

```c
void hrt_mutex_init(hrt_mutex_t *mutex);
int  hrt_mutex_lock(hrt_mutex_t *mutex);
int  hrt_mutex_try_lock(hrt_mutex_t *mutex);
int  hrt_mutex_unlock(hrt_mutex_t *mutex);
```

Mutexes are owner-tracked, non-recursive, task-context-only, and use FIFO waiters with direct handoff. Unlock uses the same scheduler-aware wake/preemption rule as other IPC paths. There is no timed lock, recursive mode, ISR API, priority inheritance, or automatic owner-death recovery.

A task must release every mutex it owns before returning or calling `hrt_task_delete()`. An EXITED task is not automatically removed as a mutex owner in v0.5.

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

The C++ `QueueRef<T>` and `StaticQueue<T, Capacity>` wrappers require trivially-copyable payloads because the C queue copies object representation with `memcpy`. `StaticQueue` also rejects capacity above the C API's `uint16_t` range at compile time.

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

    if (hrt_init(&cfg) != HRT_OK) return 1;
    if (hrt_create_task(task_a, NULL, stack_a, 2048, &a) < 0) return 1;
    if (hrt_create_task(task_b, NULL, stack_b, 2048, &b) < 0) return 1;
    return (int)hrt_start();
}
```