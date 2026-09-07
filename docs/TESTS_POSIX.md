# POSIX Test Suite

The POSIX suite validates HardRT 0.5.1 core logic and Linux-hosted scheduler integration. It is not a Cortex-M timing test.

## Execution model

- Port: `posix`
- Main executable: `hardrt_tests`
- Internal tick source: monotonic timer pthread
- External tick: covered explicitly by application-owned tick tests
- Task contexts: one pthread per live HardRT application task
- Scheduler/controller: the thread running `hrt_start()`
- Asynchronous hosted handoff: targeted process signals park/resume the active task pthread

The common HardRT core remains authoritative for task state, READY ordering, wake decisions, scheduling policy, sleeper accounting, and timeslices. The timer pthread and signal handlers request or establish a safe handoff; they do not become independent scheduler owners.

The hosted backend currently reserves process-wide `SIGALRM` for task preemption/parking and `SIGUSR2` for wake/resume handling. Host pthread scheduling and signal latency are outside HardRT's hard-real-time claims.

## Test hooks

Hosted tests build with `HARDRT_TEST_HOOKS`. Test-only hooks include scheduler stop/reset, tick fast-forward/set/get, task/slot state inspection, READY-membership inspection, idle counters, and targeted event-waiter registration/invariant helpers.

These are private test facilities and are not installed public API.

## Build and run

```bash
cmake -S . -B build-tests \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON
cmake --build build-tests --target hardrt_tests -j
ctest --test-dir build-tests --output-on-failure
```

The runtime test executable is created only for `HARDRT_PORT=posix`.

## Current coverage

The suite covers:

- version/port identity and initialization/lifecycle validation;
- task creation limits, transactional creation, runtime creation, stack-overlap rejection, EXITED-slot reclamation;
- READY/RUNNING/slot-state invariants;
- strict priority, true global RR, and priority-RR scheduling;
- explicit yield and `hrt_sleep(0)` immediate scheduling-point behavior;
- positive sleep conversion, sleeper FIFO/order, repeated sleep/wake cycles, and 32-bit tick wrap;
- runtime policy/default-slice updates;
- semaphore, queue, mutex, and external-tick contracts;
- queue wake policy/barging/waiter-overflow edges;
- event wait-any/wait-all, retained/clear-on-exit bits, overlapping/multiple waiters, invalid masks, pre-set events, and repeated set/clear cycles;
- task-notification pending-before-wait, actions, clear masks, unrelated blocking, target-state handling, bursts, saturation, EXITED/unused/invalid targets, and slot reuse;
- simultaneous event and notification wake publication;
- deterministic long-running signal stress under PRIORITY, global RR, and PRIORITY_RR;
- external tick activity interleaved with synchronization stress;
- asynchronous CPU-bound preemption where the interrupted task does not voluntarily enter HardRT;
- internal invariants for task state, slot ownership, waiter membership, notification-wait state, and READY membership.

The asynchronous-preemption regression deliberately runs a low-priority task in an infinite CPU loop without HardRT calls while a sleeping higher-priority task must wake and execute. A pass therefore proves that hosted scheduler progress no longer depends on cooperative task entry into the kernel.

The deterministic signal stress uses a fixed seed and 1024 synchronization operations per scheduler policy. Failures record the policy/iteration and primitive-specific state instead of hanging the suite.

## Hosted handoff invariants

The pthread backend adds host-specific concurrency invariants that are required in addition to the common-core READY/state rules:

- at most one HardRT application pthread is considered active at a time;
- the scheduler/controller processes pending ticks or scheduler state only after the active task has published that it is parked;
- semaphore posts are notifications, not proof of ownership transfer; duplicate/stale notifications must not pre-dispatch a future task run gate;
- an EXITED task publishes its port-side non-executability before its core slot can be reclaimed;
- `sem_wait()` paths tolerate `EINTR` because targeted signals are part of normal hosted operation.

These properties prevent host scheduling artifacts from creating execution states that the common HardRT core never selected.

## Strict warnings and UBSan

The dedicated signal-stress CI job configures:

```text
HARDRT_STRICT=ON
HARDRT_SANITIZE=ON
```

Strict warnings include `-Wall -Wextra -Wpedantic -Wconversion -Wcast-qual -Wshadow`, with `-Werror` promoting every warning to a build failure. UBSan is applied to the actual HardRT production library and the test executable with:

```text
-fsanitize=undefined -fno-omit-frame-pointer
```

AddressSanitizer is not currently part of the release gate. No ASan compatibility claim is made for the pthread/signal hosted runtime until dedicated ASan validation exists.

`hardrt_tests` also has a CTest timeout so a broken synchronization test becomes a bounded CI failure rather than an immortal runner.

## Installed-package validation

The Linux CI installs HardRT and builds a separate CMake consumer through `find_package(HardRT)`. For a POSIX package, `HardRT::hardrt` must carry `Threads::Threads` transitively and `HardRTConfig.cmake` must resolve the Threads dependency before importing the exported targets. This prevents an in-tree-only success from hiding a broken installed package.

## What a passing suite demonstrates

On the tested Linux environment, a passing suite provides evidence that the registered lifecycle, scheduler, time, synchronization, event/notification, asynchronous-preemption, hosted-handoff, and invariant contracts hold without detected UBSan errors in the strict stress configuration.

It does not establish Cortex-M timing bounds, portability to every libc/Unix implementation, POSIX signal latency bounds, or absence of defects outside covered cases. Cortex-M behavior and timing require the separate physical qualification matrix.
