# Semaphores

HardRT provides binary and counting semaphores with application-owned static storage and FIFO waiter ordering. This page describes the current `develop` behavior; v0.4.0 used a less selective wake/reschedule rule.

## Modes

- **Binary:** `max_count == 1`, with token count `0` or `1`.
- **Counting:** `max_count > 1`, with token count from `0` through `max_count`.

Extra give operations saturate at the configured maximum.

## Initialization

```c
void hrt_sem_init(hrt_sem_t *sem, unsigned init);
void hrt_sem_init_counting(
    hrt_sem_t *sem,
    unsigned init,
    uint8_t max_count);
```

Current behavior:

- `hrt_sem_init()` creates a binary semaphore and clamps `init` to `0` or `1`.
- `hrt_sem_init_counting()` changes `max_count == 0` to `1`.
- An initial value above `max_count` is clamped to `max_count`.
- Initialization clears the waiter FIFO.

## Operations

```c
int hrt_sem_take(hrt_sem_t *sem);
int hrt_sem_try_take(hrt_sem_t *sem);
int hrt_sem_give(hrt_sem_t *sem);
int hrt_sem_give_from_isr(hrt_sem_t *sem, int *need_switch);
```

### Try take

`hrt_sem_try_take()` decrements and succeeds when a token is available. It returns `-1` without blocking when the count is zero.

### Blocking take

`hrt_sem_take()` first tries the fast path. When no token is available, it:

1. enters the port critical section;
2. checks the count again;
3. appends the current task ID to the FIFO waiter queue;
4. marks the task `HRT_BLOCKED`;
5. requests rescheduling and transfers to the scheduler.

There is currently no timeout variant.

### Task-context give

When a waiter exists, `hrt_sem_give()` removes exactly one FIFO waiter, performs direct handoff, and makes that task READY rather than incrementing the stored token count.

The wake is then evaluated by the common scheduler-aware rule:

- under `HRT_SCHED_PRIORITY` and `HRT_SCHED_PRIORITY_RR`, a strictly higher-priority waiter preempts the current READY task;
- an equal- or lower-priority waiter does not force a context switch solely because it woke;
- if no normal task is running, or the recorded current task is not READY, the wake requests scheduling.

When task-context preemption is required, the scheduler request is **not** treated as an explicit yield. The interrupted task therefore retains queue precedence and any unused RR quantum.

When no waiter exists, the stored count is incremented up to `max_count`.

### ISR give and `need_switch`

`hrt_sem_give_from_isr()` performs the same direct handoff without blocking.

`need_switch` has one scheduler-aware meaning:

- `1`: the awakened waiter should run before the interrupted/current task under the active scheduler contract, or no normal task is running;
- `0`: the wake does not require an immediate scheduler handoff.

When a switch is required, `hrt_sem_give_from_isr()` requests it internally through the port-independent reschedule hook. Application ISR code does not call a second yield hook.

The selected port's ISR critical-section rules apply. Cortex-M callers must use an interrupt priority compatible with the configured `BASEPRI` syscall ceiling.

See [SCHEDULING.md](SCHEDULING.md) for the complete READY-transition and retained-quantum contract.

## Binary semaphore example

```c
static hrt_sem_t ready;

static void init_sync(void) {
    hrt_sem_init(&ready, 0);
}

static void producer(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sem_give(&ready);
        hrt_sleep(100);
    }
}

static void consumer(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sem_take(&ready);
        /* Handle one synchronization event. */
    }
}
```

A binary semaphore stores at most one pending token. Multiple gives performed while no task is waiting collapse into the single available state.

## Counting semaphore example

```c
static hrt_sem_t slots;

static void init_slots(void) {
    hrt_sem_init_counting(&slots, 0, 5);
}

static void producer(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sem_give(&slots);
        hrt_sem_give(&slots);
        hrt_sleep(300);
    }
}

static void consumer(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sem_take(&slots);
        hrt_sleep(200);
    }
}
```

## Appropriate uses

Semaphores are suitable for:

- task wake-up signaling;
- producer/consumer coordination;
- counting a fixed number of available resources.

Semaphores are not owner-tracked and should not be used where only the acquiring task may release a protected resource. Use `hrt_mutex_t` for that contract.

A semaphore does not carry payload data. When every produced item must be retained, use a queue or application ring buffer rather than relying only on a binary signal.

## Current constraints

- FIFO waiter ordering.
- No timeout operations.
- No cancellation API for a blocked waiter.
- No priority ordering of waiter queues.
- No dynamic allocation.
