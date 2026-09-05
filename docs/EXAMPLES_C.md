# Examples

HardRT 0.5.0 bundles POSIX/null examples plus STM32H755 qualification applications.

## Bundled portable examples

### C

- `examples/two_tasks`
- `examples/two_tasks_external`
- `examples/sem_basic`
- `examples/sem_counting`
- `examples/mutex_basic`
- `examples/queue_posix`
- `examples/event_notify`

### C++

- `examples/two_tasks_cpp`
- `examples/sem_basic_cpp`
- `examples/sem_counting_cpp`
- `examples/mutex_basic_cpp`
- `examples/queue_posix_cpp`
- `examples/event_notify_cpp`

C++ examples are added only with `HARDRT_ENABLE_CPP=ON`.

## STM32H755 examples and qualification firmware

Board-specific applications include the blinky/demo examples, preemption/IPC/external-tick/BASEPRI validators, DWT timing firmware, and tick-scaling benchmark. They are cross-built by `scripts/build-stm32-examples-ci.sh` and executed on hardware through the single qualification runner:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

## Null-port example

```bash
cmake -S . -B build-null \
  -DHARDRT_PORT=null \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build-null --target two_tasks -j
./build-null/examples/two_tasks/two_tasks
```

The null port does not run task contexts or a periodic tick. `hrt_start()` returns after the build-contract stub path.

## POSIX two-task example

```bash
cmake -S . -B build-posix \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build-posix --target two_tasks -j
./build-posix/examples/two_tasks/two_tasks
```

The POSIX port uses Linux/glibc `ucontext`. Tick accounting is signal-driven, but task contexts hand control back to the scheduler only at HardRT scheduling points.

## Scheduler example

```c
#include "hardrt.h"

static uint32_t stack_a[1024];
static uint32_t stack_b[1024];

static void worker(void *arg) {
    (void)arg;
    for (;;) hrt_sleep(10);
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

    if (hrt_init(&cfg) != HRT_OK) return 1;
    if (hrt_create_task(worker, NULL, stack_a, 1024, &attr) < 0) return 1;
    if (hrt_create_task(worker, NULL, stack_b, 1024, &attr) < 0) return 1;
    return (int)hrt_start();
}
```

A timeslice is measured in ticks. `hrt_sleep(0)` is an immediate scheduling point in v0.5.0.

## Event flags

`examples/event_notify` demonstrates shared event flags and task notifications. Event groups use explicit static storage:

```c
#include "hardrt_event.h"

static hrt_event_t ready;

void init_signals(void) {
    hrt_event_init(&ready);
    hrt_event_set(&ready, 0x1u);
}
```

A waiter can choose wait-any or wait-all and optionally clear the matched bits on exit. ISR producers use `hrt_event_set_from_isr()` and the common scheduler-aware `need_switch` contract.

## Task notifications

Notifications require no separate synchronization object:

```c
#include "hardrt_notify.h"

int publish_to_task(int task_id) {
    return hrt_task_notify(task_id, 0x4u, HRT_NOTIFY_SET_BITS);
}
```

A notification sent before the target waits remains pending. `examples/event_notify` includes notify-before-wait behavior.

## C++ signal wrappers

```cpp
#include <hardrt_signals.hpp>

hardrt::EventFlags flags;

int publish(int task_id) {
    flags.set(0x1u);
    return hardrt::TaskNotification::notify(
        task_id, 1u, hardrt::NotifyAction::overwrite);
}
```

See [CPP.md](CPP.md) and [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md).

## Mutexes

Mutexes are owner-tracked, non-recursive, FIFO/direct-handoff, and task-context-only. v0.5.0 provides no timed lock, priority inheritance, or automatic owner-death recovery. Tasks must release owned mutexes before return/deletion.

## External tick example

`examples/two_tasks_external` selects `HRT_TICK_EXTERNAL`; the application-provided source calls `hrt_tick_from_isr()` after scheduler execution begins.

```bash
cmake -S . -B build-external \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build-external --target two_tasks_external -j
./build-external/examples/two_tasks_external/two_tasks_external
```

See [TICK_SOURCE.md](TICK_SOURCE.md).
