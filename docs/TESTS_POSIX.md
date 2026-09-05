# POSIX Test Suite

The POSIX suite validates HardRT 0.5.0 core logic and hosted scheduler integration. It is not a Cortex-M timing test.

## Execution model

- Port: `posix`
- Main executable: `hardrt_tests`
- Tick source: normally `SIGALRM`, with explicit external-tick tests as well
- Task contexts: Linux/glibc `ucontext`
- Context handoff: at HardRT scheduling points

The signal handler advances tick accounting and requests scheduling; it does not perform `swapcontext()` directly.

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
- internal invariants for task state, slot ownership, waiter membership, notification-wait state, and READY membership.

The deterministic signal stress uses a fixed seed and 1024 synchronization operations per scheduler policy. Failures record the policy/iteration and primitive-specific state instead of hanging the suite.

## Strict warnings and UBSan

The dedicated signal-stress CI job configures:

```text
HARDRT_STRICT=ON
HARDRT_SANITIZE=ON
```

Strict warnings include `-Wall -Wextra -Wpedantic -Wconversion -Wcast-qual -Wshadow`. UBSan is applied to the actual HardRT production library and the test executable with:

```text
-fsanitize=undefined -fno-omit-frame-pointer
```

AddressSanitizer is intentionally excluded because of the POSIX `ucontext` execution model.

`hardrt_tests` also has a CTest timeout so a broken synchronization test becomes a bounded CI failure rather than an immortal runner.

## What a passing suite demonstrates

On the tested Linux/glibc environment, a passing suite provides evidence that the registered lifecycle, scheduler, time, synchronization, event/notification, and invariant contracts hold without detected UBSan errors in the strict stress configuration.

It does not establish Cortex-M timing bounds, portability to every libc/Unix implementation, or absence of defects outside covered cases. Cortex-M behavior and timing require the separate physical qualification matrix.
