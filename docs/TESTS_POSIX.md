# POSIX Test Suite

This document describes the hosted test suite for HardRT v0.4.0.

## Purpose and execution model

- Port: `posix`
- Test executable: `hardrt_tests`
- Tick source: normally `SIGALRM`
- Task contexts: Linux/glibc `ucontext`
- Context handoff: cooperative at HardRT scheduling points

The signal handler advances tick accounting and marks rescheduling pending. It does not call `swapcontext()`. A task returns control to the scheduler through sleep, yield, blocking IPC, deletion, or task return.

The POSIX suite validates core logic and hosted integration. It is not a timing-accuracy test for Cortex-M.

## Test hooks

Tests compile the library and test executable with `HARDRT_TEST_HOOKS`.

Current hooks include:

- `hrt__test_stop_scheduler()`
- `hrt__test_reset_scheduler_state()`
- `hrt__test_fast_forward_ticks(uint32_t delta)`
- `hrt__test_idle_counter_reset()`
- `hrt__test_idle_counter_value()`
- `hrt__test_set_tick(uint32_t value)`
- `hrt__test_get_tick()`

These symbols are not part of a normal release build.

## Build and run

```bash
cmake -S . -B build-tests \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON
cmake --build build-tests --target hardrt_tests -j
ctest --test-dir build-tests --output-on-failure
```

`HARDRT_BUILD_TESTS` defaults to ON, but the executable is created only when `HARDRT_PORT=posix`.

The helper script also runs the suite:

```bash
./scripts/build-lib-posix.sh
```

## Current coverage

The registered test sources cover:

- version, port identity, and basic initialization;
- sleep/wake behavior and controlled scheduler shutdown;
- same-priority yield and sleep rotation;
- strict priority dominance;
- cooperative versus sliced tasks within a priority class;
- tick-rate conversion;
- task creation limits and default attributes;
- runtime policy/default-slice updates;
- FIFO ready-queue order within a priority class;
- tick wraparound;
- current `sleep(0)` behavior;
- task return;
- semaphores, queues, mutexes, and external tick mode;
- idle behavior and `hrt_now_ms()`.

The `sleep(0)` test currently verifies that `hrt_sleep(0)` delays for at least one tick. It does not treat zero as an alias for `hrt_yield()`.

The RR tests exercise tasks within the same priority class. They do not prove that `HRT_SCHED_RR` ignores task priorities; the current scheduler continues to select the highest non-empty priority queue.

## Output

Each test prints a heading, assertion results, and a suite summary. The process returns zero only when all registered cases pass.

## Sanitizer behavior

With `HARDRT_SANITIZE=ON`, the test configuration enables UndefinedBehaviorSanitizer:

```text
-fsanitize=undefined -fno-omit-frame-pointer
```

AddressSanitizer is not enabled because `makecontext()` and `swapcontext()` are not compatible with the intended ASan setup.

## What a passing suite demonstrates

A passing run provides evidence that, on the tested Linux/glibc environment:

- core tick and millisecond conversion behave as asserted by the tests;
- sleep, wake, block, and ready-queue transitions complete without detected test failures;
- same-priority FIFO and slice behavior matches the registered cases;
- synchronization primitives integrate with the hosted scheduler;
- no context switch is performed directly in the SIGALRM handler;
- test-only scheduler shutdown remains deterministic.

It does not establish Cortex-M timing bounds, portability to another libc, global priority-independent round-robin semantics, or absence of defects outside the covered cases.