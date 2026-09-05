# C API Overview

This page summarizes the public C surface of the HardRT v0.5 line. Public headers, implementation, tests, and examples define one contract; historical v0.4 behavior is mentioned only where migration requires it.

## Core types and configuration

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

Priority `0` is the highest priority. `HARDRT_CFG_MAX_PRIO` selects the configured number of priority classes and `HARDRT_CFG_MAX_TASKS` selects application-task capacity. The build reserves one additional private idle-task slot.

Task execution state and TCB-slot ownership are private implementation details. A used application task can be `READY`, `RUNNING`, `SLEEP`, `BLOCKED`, or `EXITED`; an EXITED task remains in a used slot until later task creation reclaims it.

## Version and port identity

```c
const char *hrt_version_string(void);
unsigned    hrt_version_u32(void);
const char *hrt_port_name(void);
int         hrt_port_id(void);
```

`hrt_version_u32()` encodes `(major << 16) | (minor << 8) | patch`.

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

The lifecycle is:

```text
UNINITIALIZED -> INITIALIZED -> RUNNING
```

A second initialization or scheduler start is rejected rather than silently resetting kernel state.

`hrt_init(NULL)` selects a 1000 Hz tick, `HRT_SCHED_PRIORITY_RR`, a default five-tick slice, `core_hz = 0`, and `HRT_TICK_SYSTICK`. Explicit configurations must provide a non-zero tick rate, a declared scheduler policy, and a declared tick source. A selected port may impose narrower representable limits and return `HRT_ERR_PORT_INIT` when it cannot configure the request.

`hrt_create_task()` requires a non-null entry, non-null stack, at least 64 stack words, valid priority, and a non-overlapping stack range. Task creation is transactional. Live task stacks may not overlap. EXITED slots and their stacks can be reclaimed safely. Runtime task creation after `hrt_start()` is supported; the new task joins READY at the next scheduling point rather than forcing immediate preemption merely because it was created.

## Scheduling policies

- `HRT_SCHED_PRIORITY`: strict fixed-priority FIFO scheduling; only a strictly higher-priority wake requests preemption.
- `HRT_SCHED_RR`: one global FIFO independent of priority; wakes join the tail and do not steal the running task's remaining quantum.
- `HRT_SCHED_PRIORITY_RR`: strict priority selection with round-robin only inside a priority class; higher-priority preemption preserves the interrupted task's queue precedence and unused quantum.
- `timeslice == 0`: disables tick-driven rotation for that task.

Runtime switching among the three policies is supported with:

```c
void hrt_set_policy(hrt_policy_t policy);
void hrt_set_default_timeslice(uint16_t ticks);
```

Changing policy is a scheduling point. `hrt_set_default_timeslice()` affects subsequently default-created tasks only.

See [SCHEDULING.md](SCHEDULING.md).

## Task control and time

```c
void     hrt_sleep(uint32_t ms);
void     hrt_yield(void);
void     hrt_task_delete(void);
uint32_t hrt_tick_now(void);
uint32_t hrt_now_ms(void);
```

Positive sleep durations are converted with ceiling division so a positive sub-tick delay sleeps for one tick. In v0.5, `hrt_sleep(0)` is an immediate scheduling point equivalent to a yield for scheduling purposes; it does not enter the sleep queue.

`hrt_task_delete()` moves the current non-idle task to EXITED and yields. Task trampolines perform the same transition automatically when task entry returns.

`hrt_tick_now()` returns the wrapping 32-bit tick count. `hrt_now_ms()` uses a 64-bit intermediate when converting ticks to milliseconds.

## External tick API

```c
void hrt_tick_from_isr(void);
```

This function is valid only when `HRT_TICK_EXTERNAL` is selected. It advances kernel time, sleeper processing, and slice accounting, and requests rescheduling when required. Calling it while a port-owned SysTick source is selected records a tick-source mismatch and does not advance time.

An application-owned periodic tick must not begin calling into HardRT before scheduler execution has started.

See [TICK_SOURCE.md](TICK_SOURCE.md).

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

Binary semaphores saturate at one. Counting semaphores saturate at `max_count`; zero maximum is normalized to one. Waiters are FIFO. Task and ISR wake paths use the scheduler-aware preemption decision. ISR give is non-blocking and exposes that decision through `need_switch`.

See [SEMAPHORES.md](SEMAPHORES.md).

## Mutexes

```c
void hrt_mutex_init(hrt_mutex_t *mutex);
int  hrt_mutex_lock(hrt_mutex_t *mutex);
int  hrt_mutex_try_lock(hrt_mutex_t *mutex);
int  hrt_mutex_unlock(hrt_mutex_t *mutex);
```

Mutexes are task-context-only, owner-tracked, non-recursive, and use FIFO waiters with direct handoff. v0.5 does not provide timed lock, priority inheritance, or automatic owner-death recovery. A task must release every mutex it owns before returning or deleting itself.

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

Queues are fixed-capacity, caller-storage, copy-based FIFOs. Items are copied with `memcpy` while the queue critical section is held. Blocking operations wait indefinitely. ISR variants are non-blocking and use scheduler-aware `need_switch` behavior.

See [QUEUES.md](QUEUES.md).

## Event flags

Include:

```c
#include "hardrt_event.h"
```

The public event payload is a 32-bit word:

```c
typedef uint32_t hrt_event_bits_t;

typedef enum {
    HRT_EVENT_WAIT_ANY = 0u,
    HRT_EVENT_WAIT_ALL = 1u << 0,
    HRT_EVENT_CLEAR_ON_EXIT = 1u << 1
} hrt_event_wait_option_t;
```

Operations are:

```c
void             hrt_event_init(hrt_event_t *event);
hrt_event_bits_t hrt_event_get(const hrt_event_t *event);
int              hrt_event_set(hrt_event_t *event,
                               hrt_event_bits_t bits);
int              hrt_event_set_from_isr(hrt_event_t *event,
                                        hrt_event_bits_t bits,
                                        int *need_switch);
int              hrt_event_clear(hrt_event_t *event,
                                 hrt_event_bits_t bits);
int              hrt_event_clear_from_isr(hrt_event_t *event,
                                          hrt_event_bits_t bits);
int              hrt_event_wait(hrt_event_t *event,
                                hrt_event_bits_t mask,
                                unsigned options,
                                hrt_event_bits_t *matched);
```

`hrt_event_wait()` requires a non-zero mask. Wait-any is the default; wait-all requires every requested bit. With `HRT_EVENT_CLEAR_ON_EXIT`, the matched bits are cleared after all waiters satisfied by the same set have been evaluated against one common post-set snapshot.

Waiter registration order is FIFO. A set operation inspects a bounded number of waiter records, at most the configured application-task capacity, and publishes every satisfied waiter READY. The active scheduler policy determines actual execution order after publication.

Task-context and ISR set operations can wake waiters. Clear operations never wake tasks. Setting or clearing zero bits is a valid no-op.

## Task notifications

Include:

```c
#include "hardrt_notify.h"
```

Each application task owns one private 32-bit notification value plus pending/wait state. Producer actions are:

```c
typedef enum {
    HRT_NOTIFY_SET_BITS = 0,
    HRT_NOTIFY_OVERWRITE,
    HRT_NOTIFY_NO_OVERWRITE,
    HRT_NOTIFY_INCREMENT
} hrt_notify_action_t;
```

Operations are:

```c
int hrt_task_notify(int task_id,
                    uint32_t value,
                    hrt_notify_action_t action);
int hrt_task_notify_from_isr(int task_id,
                             uint32_t value,
                             hrt_notify_action_t action,
                             int *need_switch);
int hrt_task_notify_wait(uint32_t clear_on_entry,
                         uint32_t clear_on_exit,
                         uint32_t *value);
uint32_t hrt_task_notify_take(int clear_count_on_exit);
```

A successful producer marks the notification pending. `SET_BITS` ORs the supplied value, `OVERWRITE` replaces it, `NO_OVERWRITE` rejects an update while another notification is pending, and `INCREMENT` adds one with `UINT32_MAX` saturation while ignoring its `value` argument.

A notification wakes a task only when that task is blocked specifically on its notification. Sending to a READY or RUNNING task updates pending state without changing scheduler membership. A notification remains pending while the target is blocked on an unrelated semaphore, queue, mutex, event, or sleep.

`hrt_task_notify_wait()` applies `clear_on_entry` before testing pending state, returns the pre-exit-clear value, applies `clear_on_exit`, and consumes pending state. A notification sent before the wait therefore returns immediately when the task later waits.

`hrt_task_notify_take()` blocks until the notification value is non-zero and provides counting semantics: either clear the stored count to zero or decrement it by one.

Valid producer targets are live application tasks. Idle, unused, invalid, and EXITED targets are rejected.

## ISR wake contract

ISR producer functions are non-blocking. When they make a task READY, `need_switch` reports whether that task should run before the interrupted/current task under the active scheduler policy. HardRT pends a context switch through the port; ISR APIs do not directly execute another task.

On Cortex-M, applications must respect the documented interrupt-priority ceiling for ISR calls that enter HardRT critical sections. See [PORTING.md](PORTING.md) and [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md).

## Allocation and compatibility

The kernel and synchronization primitives use static or caller-provided storage. Event objects contain waiter metadata sized by configured application-task capacity. Notification state lives in the private TCB. Neither feature allocates dynamically or creates a worker task.

Concrete public synchronization object layouts are not an ABI guarantee across pre-1.0 minor releases. See [COMPATIBILITY.md](COMPATIBILITY.md).

## Minimal task example

```c
#include "hardrt.h"

static uint32_t stack_a[2048];
static uint32_t stack_b[2048];

static void task_a(void *arg) {
    (void)arg;
    for (;;) hrt_sleep(500);
}

static void task_b(void *arg) {
    (void)arg;
    for (;;) hrt_sleep(1000);
}

int main(void) {
    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_SYSTICK
    };
    const hrt_task_attr_t a = {.priority = HRT_PRIO0, .timeslice = 0};
    const hrt_task_attr_t b = {.priority = HRT_PRIO1, .timeslice = 5};

    if (hrt_init(&cfg) != HRT_OK) return 1;
    if (hrt_create_task(task_a, NULL, stack_a, 2048, &a) < 0) return 1;
    if (hrt_create_task(task_b, NULL, stack_b, 2048, &b) < 0) return 1;
    return (int)hrt_start();
}
```
