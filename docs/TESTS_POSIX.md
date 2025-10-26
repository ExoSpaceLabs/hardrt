# POSIX Test Suite — Validating HeaRTOS on Linux

This document explains how the POSIX-hosted test suite validates the HeaRTOS static library, how to build and run it, and what `HEARTOS_TEST_HOOKS` means.

## Overview
- Target: POSIX port (`HEARTOS_PORT=posix`)
- Artifact: `heartos_tests` (single executable)
- Purpose: Deterministically exercise core scheduling/time behavior on a desktop host using a signal-driven tick and cooperative context switching.
- Output: Numbered test cases with colorized PASS/FAIL lines and a final summary.

The suite is self-contained; it does not pull any external test framework.

## What is HEARTOS_TEST_HOOKS?
Some tests need additional visibility or control to be deterministic (e.g., stop the scheduler cleanly, fast-forward ticks, inspect idle activity). To support this, the POSIX port and core expose a few extra symbols guarded by the compile-time macro `HEARTOS_TEST_HOOKS`.

- When building tests via CMake on the POSIX port, `HEARTOS_TEST_HOOKS` is REQUIRED and is automatically defined for the library target:
  - `target_compile_definitions(heartos PRIVATE HEARTOS_TEST_HOOKS)`
- These hooks do not exist in normal (non-test) builds and have no impact on release functionality.
- In this project’s configuration, POSIX tests are always built with hooks enabled; if you manually disable them, hook-dependent tests will fail fast rather than silently skipping, because they validate essential scheduler behavior.

Examples of test-only hooks (POSIX):
- `hrt__test_stop_scheduler()` / `hrt__test_reset_scheduler_state()` — deterministic start/stop of the scheduler loop.
- `hrt__test_fast_forward_ticks(uint32_t delta)` — advance the core tick with `SIGALRM` masked (used for wraparound testing).
- `hrt__test_idle_counter_reset()` / `hrt__test_idle_counter_value()` — observe idle-loop activity.
- Core helpers under tests: `hrt__test_set_tick(uint32_t)` / `hrt__test_get_tick()`.

## Building and running

### With CLion’s Debug profile (recommended here)
```
cmake --build /home/dev/Works/heartos/cmake-build-debug --target heartos_tests -j && \
/home/dev/Works/heartos/cmake-build-debug/heartos_tests
```

Or via CTest (single test target wired in CMake):
```
cmake --build /home/dev/Works/heartos/cmake-build-debug --target heartos_tests -j && \
ctest --test-dir /home/dev/Works/heartos/cmake-build-debug -V
```

Prerequisites at configure time:
- `-DHEARTOS_PORT=posix`
- `-DHEARTOS_BUILD_TESTS=ON` (default ON)

If either is missing or the port is not `posix`, the `heartos_tests` target is skipped.

### Using the helper script
The helper script also builds and runs the test suite before launching the example:
```
./scripts/build-lib-posix.sh
```
It will abort before running the example if any test fails.

## What the suite covers
Existing groups (non-exhaustive summary):
- Identity & init basics
- Sleep/wake determinism and controlled scheduler stop
- Round-robin fairness with yield and with short sleeps
- Strict priority dominance (PRIORITY policy)
- Cooperative vs RR mix within the same priority class
- Tick-rate independence (e.g., 200 Hz) via correct ms→tick conversion
- Task creation limits, default attributes behavior
- Runtime tuning at runtime (policy switch)
- Ready-queue FIFO order in a priority class
- Tick wraparound safety (requires `HEARTOS_TEST_HOOKS`)
- `sleep(0)` semantics vs `yield()`
- Task return stability (task entry returns without crashing the scheduler)

All tests are deterministic and bounded; the POSIX scheduler is stopped by a test hook when a case is complete.

## Output format
Each case prints:
- A heading: `==== Test N/TOTAL: <Name> ====`
- One or more colorized assertion lines:
  - `PASS: <description>` (green)
  - `FAIL: <description>` with diagnostics (red)
- A per-test result line and an end-of-suite summary:
```
================ Summary ================
Total tests: <N>
  Passed: <n>
  Failed: <m>
========================================
```
The test runner returns exit code 0 if all tests passed; non-zero otherwise.

## Skipped tests and hook-dependent behavior
- Some cases (e.g., tick wraparound) rely on `HEARTOS_TEST_HOOKS`. If the library under test was not built with these hooks, such cases are reported as skipped in their group implementation or omitted from registration.
- In the default POSIX test configuration provided by this project, hooks are enabled, so no cases are skipped.

## Validating the library
For the POSIX port, a successful run of `heartos_tests` indicates that:
- The core time base (`hrt_tick_now`) is consistent with the configured tick rate.
- Sleep/wake and round-robin accounting behave as specified.
- Priority policy invariants hold (strict priority wins, FIFO within a class).
- The port integrates correctly with the core: no context switches in the signal handler; rotation at scheduler re-entry with `SIGALRM` masked.

This provides a strong signal that the static library (`libheartos.a`) is functionally correct on the POSIX host port.
