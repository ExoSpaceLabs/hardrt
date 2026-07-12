# Semaphores

HardRT v0.4.0 provides binary and counting semaphores with application-owned static storage and FIFO waiter ordering.

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

There is no timeout variant in v0.4.0.

### Task-context give

When a waiter exists, `hrt_sem_give()` removes exactly one FIFO waiter and passes it to the ready-queue insertion path. The token is handed directly to that waiter rather than incrementing the stored count.

The current implementation then calls `hrt_yield()` whenever a waiter was awakened. It does this regardless of the relative priorities of the giver and waiter.

When no waiter exists, the stored count is incremented up to `max_count`.

### ISR give

`hrt_sem_give_from_isr()` performs the same direct handoff without blocking.

In v0.4.0:

- `*need_switch` is set to `1` whenever any waiter was awakened;
- the value does not specifically mean that a higher-priority task was awakened;
- the function calls `hrt__pend_context_switch()` itself when a waiter is awakened;
- the ISR does not call an additional yield function.

The selected port's ISR critical-section rules apply. Cortex-M callers must use an interrupt priority compatible with the configured `BASEPRI` syscall ceiling.

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
- `need_switch` reports that a waiter was awakened, not a completed priority comparison.