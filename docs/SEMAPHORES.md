## 🔒 Semaphores (binary + counting)

HardRT provides a small, predictable semaphore primitive for task synchronization and resource management.

There are two modes:

- **Binary semaphore:** `max_count = 1`, tokens are `0/1`. Extra `give()` calls saturate at `1`.
- **Counting semaphore:** `max_count > 1`, tokens range `0..max_count`. Extra `give()` calls saturate at `max_count`.

Semaphores are deliberately deterministic:

- **FIFO waiter queue:** tasks that block in `hrt_sem_take()` are queued FIFO.
- **Direct handoff on wake:** when a `give()` finds a waiter, it wakes exactly one task. The token is handed off to that task rather than accumulated separately.
- **ISR-safe give:** `hrt_sem_give_from_isr()` can be called from ISR/tick context and reports whether a context switch is needed.

---

## API

### Initialization

```c
void hrt_sem_init(hrt_sem_t *s, unsigned init);
void hrt_sem_init_counting(hrt_sem_t *s, unsigned init, uint8_t max_count);
```

- `hrt_sem_init()` creates a binary semaphore. `init` is treated as `0/1`.
- `hrt_sem_init_counting()` creates a counting semaphore:
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
- `try_take()` succeeds only if at least one token is available.
- `take()` blocks if no token is available.
- `give()`:
  - If there is a waiter: wakes exactly one waiter by direct handoff.
  - If there is no waiter: increments the token count, saturating at `max_count`.
- `give_from_isr()` behaves like `give()` but sets `*need_switch = 1` if the wake should trigger rescheduling after the ISR.

---

## Use semaphores for the right thing

Semaphores are appropriate for:
- event signaling
- producer/consumer wakeups
- counting available resources

---

## Examples

### Binary semaphore (event gate)

```c
static hrt_sem_t sem;

void init(void) {
    hrt_sem_init(&sem, 0);
}

void producer(void*) {
    for (;;) {
        hrt_sem_give(&sem);
        hrt_sleep(100);
    }
}

void consumer(void*) {
    for (;;) {
        hrt_sem_take(&sem);
        /* handle event */
    }
}
```

### Counting semaphore (resource pool)

```c
static hrt_sem_t slots;

void init(void) {
    hrt_sem_init_counting(&slots, 0, 5);
}

void producer(void*) {
    for (;;) {
        hrt_sem_give(&slots);
        hrt_sem_give(&slots);
        hrt_sleep(300);
    }
}

void consumer(void*) {
    for (;;) {
        hrt_sem_take(&slots);
        hrt_sleep(200);
    }
}
```

---

## Notes and guarantees

- Saturation is intentional.
- For never-lose-data semantics, a semaphore alone is not a queue. Use a queue or ring buffer for the payload, optionally with a semaphore as the wakeup signal.
- No timeout variants are present yet in the current public API.
