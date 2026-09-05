# Documentation Summary

This documentation set describes the HardRT v0.5 development contract and preserves historical notes where v0.4.0 behavior differs. Planned roadmap items are not part of the current contract until implementation, tests, and user documentation agree.

## Current v0.5 qualification status

The scheduler/lifecycle hardening phase and the first event/notification functional hardware pass are accepted development evidence. The final v0.5.0 release candidate must repeat the complete unfiltered STM32H755 suite on the exact frozen RC SHA.

The single hardware entry point is:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The current runner contains **13 functional contracts and 38 benchmark images**, including the v0.5 event/notification timing matrix. Broader 1.0-quality WCET/interference work remains separate and does not turn measured maxima into universal guarantees.

## Main documents

| File | Purpose |
|---|---|
| [Main README](../README.md) | Project overview and current behavior |
| [INTRODUCTION.md](INTRODUCTION.md) | Scope and design goals |
| [HARD_REAL_TIME.md](HARD_REAL_TIME.md) | Hard real-time qualification boundary and remaining work |
| [QUALIFICATION.md](QUALIFICATION.md) | Hardware qualification and release-evidence policy |
| [STM32_MANUAL_TESTS.md](STM32_MANUAL_TESTS.md) | STM32H755 functional and timing matrix |
| [BUILD.md](BUILD.md) | CMake prerequisites, options, build, and install behavior |
| [API_C.md](API_C.md) | Public C API behavior |
| [CPP.md](CPP.md) | C++17 wrapper API |
| [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md) | Event-flag and per-task notification semantics |
| [SCHEDULING.md](SCHEDULING.md) | Scheduler, preemption, RR, and `need_switch` contract |
| [PORTING.md](PORTING.md) | Port hooks and execution model |
| [TICK_SOURCE.md](TICK_SOURCE.md) | Internal versus external tick ownership |
| [SEMAPHORES.md](SEMAPHORES.md) | Binary and counting semaphore behavior |
| [MUTEXES.md](MUTEXES.md) | Mutex behavior and limitations |
| [QUEUES.md](QUEUES.md) | Queue behavior, storage, and ISR operations |
| [EXAMPLES_C.md](EXAMPLES_C.md) | Bundled example overview |
| [MODULE_STATUS.md](MODULE_STATUS.md) | Source-module status |
| [TESTS_POSIX.md](TESTS_POSIX.md) | POSIX test-harness behavior and limits |
| [STATISTICS.md](STATISTICS.md) | STM32H755 timing measurements and interpretation limits |
| [COMPATIBILITY.md](COMPATIBILITY.md) | Pre-1.0 source/API, ABI, behavioral, and package policy |
| [ROADMAP.md](ROADMAP.md) | Remaining roadmap work |
| [DOCUMENTATION.md](DOCUMENTATION.md) | Doxygen and documentation guidance |

## Current synchronization surface

HardRT v0.5 provides:

- binary and counting semaphores;
- owner-tracked non-recursive mutexes;
- fixed-size copy-based message queues;
- statically allocated 32-bit event flags with wait-any/wait-all and clear-on-exit behavior;
- one private 32-bit notification word plus pending/wait state per application task;
- task-context and ISR-safe event/notification producers with scheduler-aware `need_switch` behavior.

Event updates inspect a bounded waiter set. Task-notification producer work is O(1). Neither feature allocates dynamically or creates hidden worker tasks. Generic IPC timeout variants and mutex priority inheritance remain later work.

## Accuracy rule

Public headers, implementation, tests, examples, and documentation are one contract. CI is expected to catch documentation-originated compile failures, broken internal links/paths, and stale release-facing references. If documentation and source diverge, fix the inconsistency rather than preserving two competing definitions of reality.
