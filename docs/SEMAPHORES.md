## 🔒 Semaphores (binary + counting)

HardRT provides a small, predictable semaphore primitive for task synchronization and resource management.

There are two modes:

- **Binary semaphore (legacy / default):** `max_count = 1`, tokens are `0/1`. Extra `give()` calls saturate at `1`.
- **Counting semaphore:** `max_count > 1`, tokens range `0..max_count`. Extra `give()` calls saturate at `max_count`.

Semaphores are deliberately deterministic:

- **FIFO waiter queue:** tasks that block in `hrt_sem_take()` are queued FIFO (within a priority class); global scheduling rules still apply.
- **Direct handoff on wake:** when a `give()` finds a waiter, it wakes exactly one task. The token is *handed off* to that task (no extra token is stored).
- **ISR-safe give:** `hrt_sem_give_from_isr()` can be called from ISR/tick context and reports whether a context switch is needed.

---

## API

### Initialization

```c
void hrt_sem_init(hrt_sem_t *s, unsigned init);
void hrt_sem_init_counting(hrt_sem_t *s, unsigned init, uint8_t max_count);
```

- `hrt_sem_init()` creates a **binary** semaphore. `init` is treated as `0/1`.
- `hrt_sem_init_counting()` creates a **counting** semaphore:
  - `max_count` is clamped to at least `1`.
  - `init` is clamped to `max_count`.

### Take / Try / Give

```c
int hrt_sem_take(hrt_sem_t *s);
int hrt_sem_try_take(hrt_sem_t *s);
int hrt_sem_give(hrt_sem_t *s);
int hrt_sem_give_from_isr(hrt_sem_t *s, int *need_switch);
```

Behavior:
- `try_take()` succeeds only if at least one token is available (it consumes one token).
- `take()` blocks if no token is available.
- `give()`:
  - If there is a waiter: wakes exactly one waiter (direct handoff).
  - If there is no waiter: increments the token count, saturating at `max_count`.
- `give_from_isr()` behaves like `give()` but sets `*need_switch = 1` if the wake should trigger rescheduling after the ISR.

---

## Examples

### Binary semaphore (mutual exclusion)

```c
static hrt_sem_t sem;

void init(void) {
    hrt_sem_init(&sem, 1);
}

void task(void*) {
    for (;;) {
        hrt_sem_take(&sem);
        /* critical section */
        hrt_sem_give(&sem);
    }
}
```

### Counting semaphore (resource pool)

```c
static hrt_sem_t slots;

void init(void) {
    /* 0 initial tokens, up to 5 outstanding */
    hrt_sem_init_counting(&slots, 0, 5);
}

void producer(void*) {
    for (;;) {
        /* produce up to 3 units of work */
        hrt_sem_give(&slots);
        hrt_sem_give(&slots);
        hrt_sem_give(&slots);
        hrt_sleep(300);
    }
}

void consumer(void*) {
    for (;;) {
        /* consume one unit of work */
        hrt_sem_take(&slots);
        hrt_sleep(200);
    }
}
```

---

## Notes and guarantees

- **Saturation is intentional:** if `give()` is called more than `max_count` times without corresponding takes, the semaphore caps at `max_count`.
- For “never lose events” semantics, a semaphore alone is not a queue. Use a queue/ring buffer for the data, and (optionally) a semaphore as a wakeup mechanism.
