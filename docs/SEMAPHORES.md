## 🔒 Semaphores (binary)

HeaRTOS provides a minimal binary semaphore primitive for simple mutual exclusion and task synchronization. Semaphores in HeaRTOS are deliberately small and predictable:

- Binary only: the `count` is either 0 (unavailable) or 1 (available).
- FIFO waiter queue: tasks that block in `hrt_sem_take` are queued in first-in, first-out order within a priority class; global scheduling rules still apply (fixed priority with optional round‑robin within a class).
- Immediate handoff: when a `give` wakes a waiter and we are in task context, the giver yields so that the woken (possibly higher‑priority) task can run immediately.
- ISR-safe give: `hrt_sem_give_from_isr` can be called from tick/ISR context and will pend a context switch; the actual switch happens when returning to the scheduler.

This document summarizes the API and the behavioral guarantees, and shows a small example.

---

### API

Header: `#include "heartos_sem.h"`

```c
/* Type (binary semaphore) */
typedef struct {
    volatile uint8_t count;      /* 0 or 1 */
    uint8_t          q[HEARTOS_MAX_TASKS];
    uint8_t          head, tail, count_wait; /* internal FIFO */
} hrt_sem_t;

/* Initialize with initial state: init=0 (empty) or init=1 (available). */
static inline void hrt_sem_init(hrt_sem_t* s, unsigned init);

/* Blocking take: waits until the semaphore becomes available; returns 0. */
int hrt_sem_take(hrt_sem_t* s);

/* Non‑blocking take: returns 0 if acquired, -1 if not available. */
int hrt_sem_try_take(hrt_sem_t* s);

/* Give from task context: returns 0. Wakes exactly one waiter if present. */
int hrt_sem_give(hrt_sem_t* s);

/* Give from ISR/tick context: sets *need_switch=1 if a waiter was woken. */
int hrt_sem_give_from_isr(hrt_sem_t* s, int* need_switch);
```

See the authoritative declarations in `inc/heartos_sem.h`.

---

### Behavior and guarantees

- Binary semantics
  - Multiple `give()` calls without intervening `take()` do not overflow: the internal `count` remains 1; only one subsequent `take()` will succeed.
- Waiter queueing
  - Tasks that call `hrt_sem_take()` when `count==0` are enqueued in FIFO order inside the semaphore.
  - When `hrt_sem_give()` or `hrt_sem_give_from_isr()` runs and waiters exist, exactly one waiter is woken and made READY via the core scheduler.
- Priority and preemption
  - If the woken waiter has a higher priority than the giver, it will preempt immediately. In task context, `hrt_sem_give()` yields after waking a waiter to minimize latency.
  - On ISR/tick context, `hrt_sem_give_from_isr()` pends a switch and optionally reports `need_switch=1` so the port can switch as soon as it is safe.
- Timing and RR fairness
  - Within a priority class that uses round‑robin, READY tasks continue to be rotated by their time‑slice rules. The semaphore’s FIFO determines which waiter becomes READY next; RR then applies among all READY tasks of that class.
- Safety and constraints
  - `hrt_sem_take()` before the scheduler is started (or without a current task) returns `-1`.
  - The internal wait queue capacity is bounded by `HEARTOS_MAX_TASKS`.

---

### Example

Minimal two‑task example (full example at `examples/sem_basic`):

```c
#include "heartos.h"
#include "heartos_sem.h"
#include <stdio.h>

static uint32_t sa[2048], sb[2048];
static hrt_sem_t sem;

static void A(void*){
    for(;;){
        hrt_sem_take(&sem);
        puts("[A] got sem");
        hrt_sleep(200);
        hrt_sem_give(&sem);
        hrt_sleep(200);
    }
}

static void B(void*){
    for(;;){
        if (hrt_sem_try_take(&sem)==0){
            puts("[B] got sem");
            hrt_sleep(100);
            hrt_sem_give(&sem);
        } else {
            puts("[B] waiting");
        }
        hrt_sleep(100);
    }
}

int main(){
    hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=5 };
    hrt_init(&cfg);
    hrt_sem_init(&sem, 1);
    hrt_task_attr_t p0={.priority=HRT_PRIO0,.timeslice=0}, p1={.priority=HRT_PRIO1,.timeslice=5};
    hrt_create_task(A,0,sa,2048,&p0);
    hrt_create_task(B,0,sb,2048,&p1);
    hrt_start();
}
```

Build and run this example via the existing CMake target when examples are enabled:

```bash
cmake --build cmake-build-debug --target sem_basic && \
cmake-build-debug/examples/sem_basic/sem_basic
```

> Note: The POSIX port (`-DHEARTOS_PORT=posix`) is required to observe scheduler behavior on a host machine.

---

### Testing and verification

The POSIX test suite includes dedicated semaphore tests to prevent regressions:

- Try/take/give basic semantics
- Blocking take wakes on give (no hang; watchdog protected)
- FIFO wait order for same‑priority waiters
- Double give remains binary
- ISR give wakes a waiter and indicates `need_switch`

Build and run all tests:

```bash
cmake --build cmake-build-debug --target heartos_tests && \
cmake-build-debug/heartos_tests
```

---

### Notes for ports

- Ports must provide short critical sections (`hrt_port_crit_enter/exit`) that prevent preemption while the semaphore manipulates its state and wait queue.
- `hrt_sem_give_from_isr` must be callable from the tick/ISR context and should not perform a context switch directly; it only pends a switch via `hrt__pend_context_switch()`.

---

### Limitations and future work

- Only binary semaphores are provided. Counting semaphores and mutexes (with priority inheritance) are out of scope for the minimal core but may be added as optional modules if they do not compromise predictability and size.
- The internal wait queue is bounded by `HEARTOS_MAX_TASKS`. Extremely contended scenarios should be sized accordingly.
