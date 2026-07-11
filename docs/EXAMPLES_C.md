# Examples

This page summarizes the example applications bundled with HardRT v0.4.0 and shows small snippets using the current API.

## Bundled examples

### C

- `examples/two_tasks`
- `examples/two_tasks_external`
- `examples/sem_basic`
- `examples/sem_counting`
- `examples/mutex_basic`
- `examples/queue_posix`
- `examples/hardrt_h755_blinky`
- `examples/hardrt_h755_demo`
- `examples/hardrt_h755_dwt_timing`

### C++

- `examples/two_tasks_cpp`
- `examples/sem_basic_cpp`
- `examples/sem_counting_cpp`
- `examples/mutex_basic_cpp`
- `examples/queue_posix_cpp`
- `examples/hardrt_h755_blinky_cpp`

C++ examples are added only when `HARDRT_ENABLE_CPP=ON`.

## Null-port example

```bash
cmake -S . -B build-null \
  -DHARDRT_PORT=null \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build-null --target two_tasks -j
./build-null/examples/two_tasks/two_tasks
```

The null port does not start a tick or transfer into task contexts. `hrt_start()` returns, so the program prints its version/port information and exits without running the task bodies.

## POSIX two-task example

```bash
cmake -S . -B build-posix \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build-posix --target two_tasks -j
./build-posix/examples/two_tasks/two_tasks
```

The POSIX port uses Linux/glibc `ucontext`. Tick accounting is signal-driven, but a running task returns to the scheduler only through a HardRT scheduling point.

## Same-priority rotation

The current scheduler always selects the highest non-empty priority queue. To observe round-robin time slicing, use tasks at the same priority with non-zero slices:

```c
#include "hardrt.h"
#include <stdio.h>

static uint32_t stack_a[2048];
static uint32_t stack_b[2048];

static void task_a(void *arg) {
    (void)arg;
    for (;;) {
        puts("A");
        hrt_sleep(10);
    }
}

static void task_b(void *arg) {
    (void)arg;
    for (;;) {
        puts("B");
        hrt_sleep(10);
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
    const hrt_task_attr_t attr = {
        .priority = HRT_PRIO1,
        .timeslice = 5
    };

    hrt_init(&cfg);
    hrt_create_task(task_a, NULL, stack_a, 2048, &attr);
    hrt_create_task(task_b, NULL, stack_b, 2048, &attr);
    hrt_start();
}
```

A timeslice is measured in ticks. In the POSIX port, a CPU-bound task that never calls a HardRT scheduling point is not asynchronously swapped out by `SIGALRM`.

## Mutex example in C

```c
#include "hardrt.h"

static hrt_mutex_t lock;
static uint32_t producer_stack[1024];
static uint32_t consumer_stack[1024];

static void producer(void *arg) {
    (void)arg;
    for (;;) {
        hrt_mutex_lock(&lock);
        /* Critical section. */
        hrt_mutex_unlock(&lock);
        hrt_sleep(10);
    }
}

static void consumer(void *arg) {
    (void)arg;
    for (;;) {
        if (hrt_mutex_try_lock(&lock) == 0) {
            /* Short critical section. */
            hrt_mutex_unlock(&lock);
        }
        hrt_sleep(5);
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
    const hrt_task_attr_t attr = {
        .priority = HRT_PRIO1,
        .timeslice = 5
    };

    hrt_init(&cfg);
    hrt_mutex_init(&lock);
    hrt_create_task(producer, NULL, producer_stack, 1024, &attr);
    hrt_create_task(consumer, NULL, consumer_stack, 1024, &attr);
    hrt_start();
}
```

Mutexes are owner-tracked, non-recursive, and task-context-only. They have no timed lock or priority inheritance in v0.4.0.

## Mutex example in C++

```cpp
#include <hardrtpp.hpp>

static hardrt::Mutex lock;

static void worker(void *arg) {
    (void)arg;
    for (;;) {
        lock.lock();
        // Critical section.
        lock.unlock();
        hardrt::Task::sleep(10);
    }
}
```

## External tick example

`examples/two_tasks_external` initializes HardRT with `HRT_TICK_EXTERNAL`. The application-provided tick source calls `hrt_tick_from_isr()`; the POSIX port does not install its own periodic `SIGALRM` timer in this mode.

```bash
cmake -S . -B build-external \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build-external --target two_tasks_external -j
./build-external/examples/two_tasks_external/two_tasks_external
```

See [TICK_SOURCE.md](TICK_SOURCE.md) for the external-tick contract.
