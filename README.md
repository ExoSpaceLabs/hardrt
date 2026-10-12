<p align="center">
  <img src="docs/images/HardRT_logo2.png" alt="HardRT logo" width="600">
</p>

<p align="center">
  <strong>Minimal Real-Time Operating System</strong><br>
  <a href="https://github.com/ExoSpaceLabs">ExoSpaceLabs</a>
</p>

**HardRT** is a small, portable real-time operating-system kernel written in C. The core uses static allocation and has no HAL dependency.

The Cortex-M target is being developed toward an **explicitly qualified hard real-time RTOS contract**. The goal is deterministic, statically bounded kernel behavior with bounded synchronization/blocking and reproducible configuration-specific timing evidence suitable for schedulability analysis, not merely preemption or low average latency. See [HARD_REAL_TIME.md](docs/HARD_REAL_TIME.md) and [ROADMAP.md](docs/ROADMAP.md).

**Development release candidate:** `0.6.0` (published release remains `0.5.1` until tagging)

## Feature set

- **Pure C core:** no dynamic allocation and no HAL dependency in kernel runtime paths.
- **Ports:** `null`, `posix`, and `cortex_m`.
- **Static tasks:** application-owned stacks, bounded task capacity, runtime creation, and EXITED-slot reclamation.
- **Scheduling:** fixed priority, true global round-robin, and fixed-priority round-robin.
- **Task control:** sleep, yield, absolute phase-preserving `hrt_delay_until()`, deletion/return, tick/time queries, and runtime policy/default-slice tuning.
- **Semaphores:** binary/counting modes, FIFO waiters, ISR-safe give, absolute-deadline waits.
- **Mutexes:** owner-tracked, non-recursive, FIFO/direct handoff, timed locking, statically bounded transitive priority inheritance under fixed-priority policies.
- **Message queues:** fixed-capacity copy-based FIFOs with task and ISR operations; timed send/receive.
- **Event flags:** 32-bit wait-any/wait-all flags with retained/clear-on-exit behavior and ISR producers.
- **Task notifications:** one private 32-bit notification per task with set-bits, overwrite, no-overwrite, increment, wait, take, and ISR producers.
- **CMake package:** installation and `find_package(HardRT)` consumption.
- **Optional C++17 wrapper:** header-only task/IPC/signal wrappers enabled with `HARDRT_ENABLE_CPP`.

The 0.6 candidate includes a shared absolute-tick IPC timeout engine (semaphores, queues, mutexes, events and notifications), bounded transitive priority inheritance for `HRT_SCHED_PRIORITY` / `HRT_SCHED_PRIORITY_RR`, and phase-preserving periodic release. **Global RR remains priority-agnostic**, so it offers no priority-inversion bound. Automatic mutex owner-death recovery, tickless idle and high-resolution timers remain unsupported. The bounded algorithms and measured critical-section maxima are not universal WCET guarantees.

## Port behavior

The Cortex-M port uses SysTick or an application-provided external tick and performs context switching through PendSV. Critical sections preserve stricter pre-existing BASEPRI masks and use the documented HardRT interrupt-priority ceiling.

The POSIX port is a Linux hosted logic and scheduler-validation environment. Each HardRT application task executes in a pthread while the HardRT core remains authoritative for task state, READY ordering, wake decisions, round-robin accounting, and scheduling policy. A monotonic timer pthread requests internal ticks, and targeted signals asynchronously park/resume the currently running hosted task so CPU-bound code cannot indefinitely prevent a scheduler-selected task from progressing.

The hosted backend is not a hard-real-time or timing-accurate Cortex-M model. Host scheduling and signal latency remain properties of the operating system. The port currently reserves process-wide `SIGALRM` for task preemption and `SIGUSR2` for resume/wake handling. On POSIX, the application-provided HardRT task-stack buffer remains tracked for the public task-lifetime contract but is not used as the native pthread execution stack.

See [PORTING.md](docs/PORTING.md) and [COMPATIBILITY.md](docs/COMPATIBILITY.md).

## Architecture

![architecture](docs/images/Architecture.png)

HardRT is divided into three layers:

- **Application:** task functions and application-owned storage.
- **HardRT core:** lifecycle, READY/sleeper storage, timing, and synchronization primitives.
- **Port:** architecture-specific tick, critical-section, idle, stack-frame, and context-switch operations.

### Task and slot lifecycle

TCB slot ownership and task execution state are separate concepts.

A slot is `UNUSED` or `USED`. A used application task can be `READY`, `RUNNING`, `SLEEP`, `BLOCKED`, or `EXITED`.

```mermaid
stateDiagram-v2
    [*] --> READY: create into UNUSED/reclaimed slot
    READY --> RUNNING: scheduler dispatch
    RUNNING --> READY: yield / preemption / RR rotation
    RUNNING --> SLEEP: hrt_sleep(ms > 0)
    SLEEP --> READY: wake tick
    RUNNING --> BLOCKED: synchronization wait
    BLOCKED --> READY: synchronization wake
    RUNNING --> EXITED: hrt_task_delete() / task return
    EXITED --> READY: later task creation reclaims slot
```

Important invariants:

- a RUNNING application task is not present in READY storage;
- a READY application task has exactly one READY representation;
- an EXITED task does not execute again, but its slot remains USED until reclamation;
- the private idle task is outside application READY queues;
- live task-stack overlap is rejected;
- runtime-created tasks join READY at the next scheduling point.

## Repository layout

```text
hardrt/
├── inc/                    # Installed public C headers
├── src/
│   ├── core/               # Kernel implementation
│   ├── internal/           # Non-installed core/port contract
│   └── port/               # null, posix, cortex_m
├── cpp/                    # Optional C++17 wrappers
├── cmake/                  # Package files and toolchains
├── examples/               # Examples and hardware qualification firmware
├── tests/                  # Hosted tests, doc probes, installed consumers
├── scripts/                # Build and qualification helpers
├── docs/                   # Documentation
├── RELEASE_NOTES.md
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

Install:

```bash
cmake --install build --prefix "$PWD/build/install"
```

Consume from another CMake project:

```cmake
find_package(HardRT 0.6 REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

For a POSIX package, `HardRT::hardrt` carries the required thread linkage transitively.

See [BUILD.md](docs/BUILD.md).

## Scheduling

Priority `0` is highest.

- `HRT_SCHED_PRIORITY`: strict fixed-priority FIFO scheduling. A strictly higher-priority wake preempts.
- `HRT_SCHED_RR`: one global READY FIFO independent of task priority. Wakes join the tail without stealing the current task's remaining quantum.
- `HRT_SCHED_PRIORITY_RR`: strict priority selection with round-robin inside a priority class. Higher-priority preemption preserves the interrupted task's queue precedence and unused quantum.
- `timeslice == 0`: disables tick-driven rotation for that task.

Runtime switching among all three policies is supported. See [SCHEDULING.md](docs/SCHEDULING.md).

## Tick and sleep behavior

- `HRT_TICK_SYSTICK`: the selected port owns the periodic tick. Configuration occurs in `hrt_init()`, activation waits for scheduler start.
- `HRT_TICK_EXTERNAL`: the application owns the timer and calls `hrt_tick_from_isr()` from its ISR after scheduler execution has begun.

Positive `hrt_sleep(ms)` durations use ceiling conversion to ticks. `hrt_sleep(0)` is an immediate scheduling point and never enters the sleeper queue.

See [TICK_SOURCE.md](docs/TICK_SOURCE.md).

## Synchronization

### Semaphores

Binary/counting semaphores use FIFO waiters and scheduler-aware wake decisions. ISR give is supported.

### Mutexes

Mutexes are task-context-only, non-recursive, owner-tracked, FIFO/direct-handoff and provide `hrt_mutex_lock_until()`. Bounded transitive priority inheritance mitigates fixed-priority inversion; `HRT_SCHED_RR` ignores priority and does not provide that guarantee. There is no automatic owner-death recovery: tasks must release owned mutexes before returning/deleting themselves. See [MUTEXES.md](docs/MUTEXES.md).

### Message queues

Queues provide blocking/non-blocking task operations plus non-blocking ISR send/receive. Payloads are copied with `memcpy()` while the queue critical section is held; the resulting item-size-dependent critical-section bound is explicitly tracked for pre-1 qualification.

### Event flags

Event groups are statically allocated and carry one 32-bit flag word. Tasks can wait for any or all bits, optionally clearing matched bits on exit. One set operation evaluates all waiters against the same post-set snapshot before applying the union of clear-on-exit requests. ISR set is supported.

### Task notifications

Each application task owns one private 32-bit notification value plus pending/wait state. Notifications support set-bits, overwrite, no-overwrite, and saturating increment actions. They wake only tasks blocked specifically on their notification; pending values survive unrelated blocking.

See [EVENTS_NOTIFICATIONS.md](docs/EVENTS_NOTIFICATIONS.md).

## Validation

Hosted CI covers:

- POSIX C/C++ builds and tests;
- asynchronous CPU-bound hosted-preemption regression coverage;
- strict-warning + UBSan event/notification stress;
- bundled examples;
- installed CMake consumers;
- documentation-originated C/C++ compile probes and Doxygen;
- Cortex-M C/C++ cross-builds;
- STM32H755 qualification firmware cross-builds.

The release-grade physical entry point for NUCLEO-H755ZI-Q / CM7 is:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The **0.6 candidate** hardware gate is **16 functional contracts + 52 benchmark metrics across 39 build/flash images**, including 14 DWT synchronization critical-section/copy metrics. The complete development run on `beb069db` passed 16/16 + 52/52; a final run is required on the corrected release-version candidate. Historical v0.5 runs used 13 functional contracts + 38 benchmarks and must not be reused as evidence for 0.6 or as universal WCET claims.

See [QUALIFICATION.md](docs/QUALIFICATION.md), [STM32_MANUAL_TESTS.md](docs/STM32_MANUAL_TESTS.md), and [STATISTICS.md](docs/STATISTICS.md).

## Hard real-time claims

HardRT is being engineered toward configuration-specific hard-real-time guarantees on supported Cortex-M targets. Current timing results are reproducible engineering measurements for documented configurations, not universal WCET proofs.

The explicit pre-1 progression is:

- **0.6.x:** bounded IPC timeouts, bounded mutex priority inversion, absolute periodic release timing, and synchronization-critical bound decisions;
- **0.7.x:** timing/interference decomposition and configuration-specific upper-bound work;
- **0.8.x:** pre-1 kernel/API hardening, portability cleanup, static-memory accounting, and static-analysis depth;
- **0.9.x:** frozen 1.0 candidate and complete qualification campaign;
- **1.0.0:** stable, explicitly qualified hard-real-time contract for documented Cortex-M configurations.

The POSIX port is excluded from hard-real-time timing claims.

See [HARD_REAL_TIME.md](docs/HARD_REAL_TIME.md) and [ROADMAP.md](docs/ROADMAP.md).

## Compatibility

0.6.0 is a pre-1.0 minor release that adds deadline APIs and priority-inheritance state. Existing applications must rebuild against the new headers and library together; no cross-minor binary compatibility is claimed. The published 0.5.1 patch remains historical. See [COMPATIBILITY.md](docs/COMPATIBILITY.md) and [RELEASE_NOTES.md](RELEASE_NOTES.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).

## DAS v0.1.0 STM32 interoperability

The HardRT+DAS CM7 integration test uses the published
[DAS v0.1.0](https://github.com/Inczert/device-abstraction-stack/releases/tag/v0.1.0)
source, verified against the exact commit in `thirdparty/DAS.lock`.
Run `scripts/stm32_das_integration.sh /path/to/STM32CubeH7` to cross-build
the installed-package UART example. The existing **16+52** H755 hardware
qualification remains a separate STM32CubeH7-only matrix, not a DAS
integration test.
