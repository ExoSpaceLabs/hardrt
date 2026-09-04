<p align="center">
  <img src="docs/images/HardRT_logo2.png" alt="HardRT logo" width="600">
</p>

<p align="center">
  <strong>Minimal Real-Time Operating System</strong><br>
  <a href="https://github.com/ExoSpaceLabs">ExoSpaceLabs</a>
</p>

**HardRT** is a small, portable real-time operating-system kernel written in C.
The core uses static allocation and has no HAL dependency.

**Version:** `0.4.0`

## Features available in v0.4.0

- **Pure C core:** no dynamic allocation and no hardware abstraction layer in the kernel.
- **Ports:** `null`, `posix`, and `cortex_m`.
- **Static tasks:** task stacks are supplied by the application.
- **Scheduling:** fixed-priority scheduling with optional round-robin time slicing inside priority classes.
- **Task control:** `hrt_sleep()`, `hrt_yield()`, `hrt_task_delete()`, `hrt_tick_now()`, and `hrt_now_ms()`.
- **Semaphores:** binary and counting modes, FIFO waiter queues, and ISR-safe give.
- **Mutexes:** owner-tracked, non-recursive, FIFO waiter queues, and direct handoff.
- **Message queues:** fixed-size, copy-based FIFO queues with task and ISR operations.
- **CMake package:** installation and consumption through `find_package(HardRT)`.
- **Optional C++17 wrapper:** header-only wrappers enabled with `HARDRT_ENABLE_CPP`.

The current release does not provide IPC timeouts, mutex priority inheritance, event flags, task notifications, tickless idle, or high-resolution timers.

## Port behavior

The Cortex-M port uses SysTick or an application-provided external tick and performs context switching through PendSV.

The POSIX port is a logic and scheduler simulator for Linux/glibc. It uses `ucontext` and a `SIGALRM` tick. The signal handler performs tick accounting and requests rescheduling, but it does not switch task contexts directly. A POSIX task returns control to the scheduler only when it calls a HardRT scheduling point, such as sleep, yield, a blocking IPC operation, task deletion, or task return. A CPU-bound task that never reaches such a point can prevent other POSIX tasks from running.

The POSIX port is not a timing-accurate model of Cortex-M execution. It is regularly exercised on Ubuntu and GitHub-hosted Linux runners. A test-suite failure has been observed on Debian 13 with GCC 14 and glibc 2.41.

See [the porting guide](docs/PORTING.md) for the current port contract.

## Architecture

![architecture](docs/images/Architecture.png)

HardRT is divided into three layers:

- **Application:** task functions and application-owned storage.
- **HardRT core:** task state, ready queues, timing, and synchronization primitives.
- **Port:** architecture-specific tick, critical-section, idle, stack-frame, and context-switch operations.

### Task lifecycle

HardRT does not use a separate `RUNNING` state. The currently executing application task remains `HRT_READY`; scheduling changes queue membership and execution context while the task stays logically runnable.

```mermaid
stateDiagram-v2
    [*] --> UNUSED

    UNUSED --> READY: hrt_create_task()

    READY --> SLEEP: hrt_sleep()
    SLEEP --> READY: wake tick

    READY --> BLOCKED: semaphore / queue / mutex wait
    BLOCKED --> READY: IPC wake / direct handoff

    READY --> READY: hrt_yield() / RR quantum expiry
    READY --> READY: higher-priority preemption / resume

    READY --> UNUSED: hrt_task_delete() / task returns
```

A task that returns from its entry function is passed to `hrt_task_delete()`, marked unused, and is not scheduled again unless that task slot is later reused by a new task creation.

## Repository layout

```text
hardrt/
├── inc/                    # Public and port-facing headers
├── src/
│   ├── core/               # Kernel implementation
│   └── port/               # null, posix, and cortex_m ports
├── cpp/                    # Optional C++17 wrapper
├── cmake/                  # Package files and toolchains
├── examples/               # Example applications
├── tests/                  # POSIX test harness
├── scripts/                # Build and test helpers
├── docs/                   # Documentation
├── LICENSE
└── README.md
```

## Build

```bash
cmake -S . -B build \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build -j
./build/examples/two_tasks/two_tasks
```

Install the package:

```bash
cmake --install build --prefix "$PWD/build/install"
```

Consume it from another CMake project:

```cmake
find_package(HardRT 0.4.0 REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

See [BUILD.md](docs/BUILD.md) for the current prerequisites and CMake options.

## Scheduling in the current implementation

The `develop` branch contains the v0.5 scheduler contract; the released v0.4.0 behavior is noted separately where it differs.

Priority `0` is the highest priority.

- `HRT_SCHED_PRIORITY` uses strict fixed-priority FIFO scheduling. A strictly higher-priority wake preempts; equal- and lower-priority wakes wait.
- `HRT_SCHED_RR` is true global round-robin on `develop`: all READY application tasks share one FIFO and task priority is ignored. Yield or quantum expiry rotates to the global tail. A wake joins the global tail without stealing the current task's remaining quantum.
- `HRT_SCHED_PRIORITY_RR` uses strict priority selection and round-robin only inside the selected priority class. A task interrupted by higher-priority work retains its queue precedence and unused quantum.
- A task with `timeslice == 0` is cooperative and is not rotated when ticks expire.

Runtime switching among the three policies rebuilds READY membership under the kernel critical section. The running task treats a policy change as a scheduling point and joins the target policy at its tail.

v0.4.0 did **not** provide true global RR: `HRT_SCHED_RR` still selected through the priority queues in that release.

A timeslice is expressed in **ticks**, not milliseconds. Its wall-clock duration depends on `tick_hz`.

See [SCHEDULING.md](docs/SCHEDULING.md) for the complete policy and READY-transition contract.

## Tick sources

- `HRT_TICK_SYSTICK`: the selected port owns the periodic tick and calls the private core tick handler. `hrt_init()` configures this source only after core and idle state are complete; the periodic source remains inactive until `hrt_start()` enters the scheduler.
- `HRT_TICK_EXTERNAL`: the application owns the timer and calls `hrt_tick_from_isr()` from that timer ISR. HardRT does not start a periodic timer in this mode.

Calling `hrt_tick_from_isr()` while `HRT_TICK_SYSTICK` is selected does not advance time and records `ERR_TICK_SOURCE_MISMATCH` through the kernel diagnostic path.

Tick processing increments the tick counter, wakes expired sleepers, updates the running task's slice, and requests rescheduling only when a wake-up or slice expiry requires it. It never performs a task-context switch directly.

See [TICK_SOURCE.md](docs/TICK_SOURCE.md) for details.

## Task timing

`hrt_sleep(ms)` converts milliseconds to ticks using ceiling division. Positive durations shorter than one tick sleep for one tick. In v0.4.0, `hrt_sleep(0)` also sleeps for one tick; use `hrt_yield()` for an immediate voluntary scheduling point.

## Synchronization

### Semaphores

- `hrt_sem_init`, `hrt_sem_init_counting`
- `hrt_sem_take`, `hrt_sem_try_take`
- `hrt_sem_give`, `hrt_sem_give_from_isr`

Semaphores use FIFO waiter queues and direct handoff. They are not owner-tracked.

### Mutexes

- `hrt_mutex_init`, `hrt_mutex_lock`, `hrt_mutex_try_lock`, `hrt_mutex_unlock`

Mutexes are task-context-only, non-recursive, and owner-tracked. The current implementation has no priority inheritance or timed lock.

### Message queues

- `hrt_queue_init`
- blocking and non-blocking send/receive
- non-blocking ISR send/receive

Queue items are copied with `memcpy` while the queue operation holds the port critical section. Keep items small or enqueue pointers/indices whose lifetime is managed by the application.

## Timing measurements

[STATISTICS.md](docs/STATISTICS.md) contains measurements collected on an STM32H755 Cortex-M7 setup. Those values characterize that recorded configuration and workload; they are not general worst-case execution-time guarantees for every application, interrupt layout, build, or memory configuration.

## License

Apache License 2.0. See [LICENSE](LICENSE).
