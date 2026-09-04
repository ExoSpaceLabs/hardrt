# HardRT Message Queues

HardRT provides fixed-capacity, copy-based FIFO queues for communication between tasks. Queue storage is supplied by the application; the kernel performs no dynamic allocation. This page describes the current `develop` behavior; v0.4.0 used a less selective wake/reschedule rule.

## Properties

- Items have one fixed size selected at initialization.
- Items are copied with `memcpy`.
- Queue order is FIFO.
- Blocking task operations may wait indefinitely.
- Non-blocking task and ISR operations return immediately.
- Sender and receiver waiters are stored in separate FIFO task-ID queues.
- Waiter FIFO order selects which blocked task is woken first; it does not reserve an item or queue slot for that task.

## Initialization

```c
#include "hardrt_queue.h"

#define QUEUE_CAPACITY 10

typedef struct {
    uint32_t id;
    uint32_t value;
} message_t;

static hrt_queue_t queue;
static uint8_t queue_storage[QUEUE_CAPACITY * sizeof(message_t)];

static void init_queue(void) {
    hrt_queue_init(
        &queue,
        queue_storage,
        QUEUE_CAPACITY,
        sizeof(message_t));
}
```

The storage must remain valid for the queue lifetime and must contain at least `capacity * item_size` bytes.

## Task-context operations

### Blocking

```c
int hrt_queue_send(hrt_queue_t *queue, const void *item);
int hrt_queue_recv(hrt_queue_t *queue, void *out);
```

- `hrt_queue_send()` retries until space is available.
- `hrt_queue_recv()` retries until an item is available.
- A full sender joins the TX waiter FIFO and becomes `HRT_BLOCKED`.
- An empty receiver joins the RX waiter FIFO and becomes `HRT_BLOCKED`.

There are currently no timeout variants.

### Non-blocking

```c
int hrt_queue_try_send(hrt_queue_t *queue, const void *item);
int hrt_queue_try_recv(hrt_queue_t *queue, void *out);
```

Both functions return `0` on success and `-1` when the operation cannot be completed immediately.

A successful task-context operation wakes at most one opposite-side waiter. The wake then uses the same scheduler-aware rule as semaphores, mutex handoff, and sleep expiry:

- under `HRT_SCHED_PRIORITY` and `HRT_SCHED_PRIORITY_RR`, a strictly higher-priority waiter preempts the current READY task;
- an equal- or lower-priority waiter does not force a context switch solely because it woke;
- if no normal task is running, or the recorded current task is not READY, the wake requests scheduling.

A required task-context preemption is not treated as an explicit yield, so the interrupted task retains its queue precedence and any unused RR quantum.

## ISR-context operations

```c
int hrt_queue_try_send_from_isr(
    hrt_queue_t *queue,
    const void *item,
    int *need_switch);

int hrt_queue_try_recv_from_isr(
    hrt_queue_t *queue,
    void *out,
    int *need_switch);
```

These operations never block.

`need_switch` has the same scheduler-aware meaning for both ISR operations:

- `1`: the awakened waiter should run before the interrupted/current task under the active scheduler contract, or no normal task is running;
- `0`: the wake does not require an immediate scheduler handoff.

When a switch is required, the queue operation requests it internally. Application ISR code does not call a separate `hrt_port_yield_from_isr()` function.

Example:

```c
void MY_IRQ_Handler(void) {
    const message_t message = {
        .id = 99,
        .value = 1234
    };
    int need_switch = 0;

    (void)hrt_queue_try_send_from_isr(
        &queue,
        &message,
        &need_switch);

    /* HardRT has already requested scheduling when need_switch == 1. */
    (void)need_switch;
}
```

The selected port's ISR critical-section rules still apply. On Cortex-M, only interrupts compatible with the configured `BASEPRI` syscall ceiling may call HardRT ISR APIs.

See [SCHEDULING.md](SCHEDULING.md) for the complete READY-transition and retained-quantum contract.

## Wake, FIFO, and barging behavior

A successful enqueue wakes at most one receiver. A successful dequeue wakes at most one sender. Waiters are removed from the corresponding waiter FIFO and passed to the common READY-queue insertion path.

The v0.5 queue contract is **FIFO waiter selection with retry-on-resume**. The queue does not directly transfer an item to a waiting receiver and does not reserve newly available capacity for a waiting sender. The selected waiter is merely made READY and later resumes its blocking operation.

Therefore waiter FIFO order is **not** a strict completion-order or resource-reservation guarantee. Before the selected waiter actually runs, another task or ISR may consume the available item or capacity. The selected waiter then retries and may block again if the resource is no longer available. This permitted behavior is commonly called barging.

Consequences:

- RX waiter FIFO determines which blocked receiver is woken first, not which receiver is guaranteed to consume the newly queued item.
- TX waiter FIFO determines which blocked sender is woken first, not which sender is guaranteed to own newly freed capacity.
- scheduler priority and already-READY work can therefore affect which caller completes first;
- v0.5 does not claim strict queue completion fairness or starvation freedom under adversarial contention.

Strict reservation/direct-handoff semantics would require additional per-waiter or per-queue reservation state and are intentionally deferred to a later queue redesign rather than being introduced during v0.5 release hardening.

## Copy and critical-section cost

The implementation copies the complete item while holding the port critical section:

```c
memcpy(destination, source, queue->item_size);
```

Large items therefore increase interrupt-masked or signal-masked time. Prefer small values, indices, or pointers for large payloads.

When queueing pointers, HardRT copies only the pointer value. It does not manage ownership or lifetime of the pointed-to storage.

```c
typedef struct {
    void *data;
    size_t length;
} buffer_ref_t;
```

The producer and consumer must define who owns the buffer and when it may be reused.

## Current structure

The public `hrt_queue_t` contains:

- storage pointer, item size, capacity, head, tail, and item count;
- one RX waiter FIFO;
- one TX waiter FIFO.

Waiter arrays are sized by `HARDRT_APP_MAX_TASKS`, the configured application-task capacity. The private idle task is not a valid queue waiter and does not consume waiter storage.

## Constraints

- Capacity and item size cannot change after initialization.
- Blocking operations have no timeout.
- No dynamic allocation is performed.
- Queue state and item storage must outlive all users.
- ISR operations are non-blocking only; they still execute the configured critical-section mechanism and item copy.
- FIFO waiter selection does not imply reserved-resource or strict completion-order fairness.
