# Documentation Summary

This documentation set covers the released v0.4.0 surface and the implementation currently present on `develop` for the v0.5 line. Pages that describe behavior changed on `develop` state that explicitly rather than presenting development semantics as if they shipped in v0.4.0.

The roadmap and GitHub issues may describe later work. A planned API or behavior is not part of the current development contract until its implementation and tests are merged and the corresponding user documentation is updated.

## Current v0.5 hardening status

The scheduler/lifecycle hardening phase is accepted on exact `develop` SHA `80f2042f2c64053a9ea888666474c5dad5f72797` with a clean NUCLEO-H755ZI-Q CM7 run:

```text
11 / 11 functional contracts PASS
22 / 22 hardware benchmarks PASS
overall PASS
```

This is development evidence. The final v0.5.0 release candidate must repeat the complete unfiltered suite on the exact frozen RC SHA. Broader hard-real-time qualification work remains open for later releases and must not be confused with the completed v0.5 scheduler/lifecycle hardening phase.

## Main documents

| File | Purpose |
|---|---|
| [Main README](../README.md) | Project overview and current behavior |
| [INTRODUCTION.md](INTRODUCTION.md) | Scope and design goals |
| [HARD_REAL_TIME.md](HARD_REAL_TIME.md) | Hard real-time qualification boundary, current evidence and remaining work |
| [QUALIFICATION.md](QUALIFICATION.md) | STM32 hardware qualification policy, current accepted development baseline and release-evidence workflow |
| [STM32_MANUAL_TESTS.md](STM32_MANUAL_TESTS.md) | Single-runner STM32H755 functional matrix and scheduler/PendSV diagnostics |
| [BUILD.md](BUILD.md) | Current CMake prerequisites, options, build, and install behavior |
| [API_C.md](API_C.md) | Current C API behavior |
| [SCHEDULING.md](SCHEDULING.md) | Current `develop` scheduler, preemption, RR-retention, and `need_switch` contract |
| [CPP.md](CPP.md) | Existing C++17 wrapper types and methods |
| [PORTING.md](PORTING.md) | Hooks and execution model used by the current ports |
| [TICK_SOURCE.md](TICK_SOURCE.md) | Internal versus external tick ownership |
| [SEMAPHORES.md](SEMAPHORES.md) | Binary and counting semaphore behavior |
| [MUTEXES.md](MUTEXES.md) | Mutex behavior and limitations |
| [QUEUES.md](QUEUES.md) | Queue behavior, storage, and ISR operations |
| [EXAMPLES_C.md](EXAMPLES_C.md) | Bundled example overview |
| [MODULE_STATUS.md](MODULE_STATUS.md) | Current source-module and hardening status |
| [TESTS_POSIX.md](TESTS_POSIX.md) | POSIX test-harness behavior and limits |
| [STATISTICS.md](STATISTICS.md) | Current and historical STM32H755 timing measurements and interpretation limits |
| [ROADMAP.md](ROADMAP.md) | Remaining v0.5 work and non-blocking post-v0.5 hardening toward 1.0.0 |
| [DOCUMENTATION.md](DOCUMENTATION.md) | Doxygen and documentation guidance |

## Current implemented synchronization surface

- binary and counting semaphores;
- owner-tracked non-recursive mutexes;
- fixed-size copy-based message queues.

Event flags and task notifications remain planned for v0.5.0 and are not implemented yet. IPC timeouts and mutex priority inheritance are later work.

On `develop`, semaphore, queue, mutex, sleep-expiry and ISR wake paths use the same scheduler-aware READY transition rule. See [SCHEDULING.md](SCHEDULING.md) for the exact contract.

## Accuracy rule

Where documentation and implementation differ, the source and tests describe what the current binary does. Such differences should be corrected in documentation immediately and tracked as implementation issues only when a different behavior has been deliberately selected for a later release.

Compile/link validation for documentation examples is tracked by #35; until that gate is complete the documentation must not claim automatic source synchronization.
