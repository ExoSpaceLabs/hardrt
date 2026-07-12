# Documentation Summary

This documentation set describes the HardRT v0.4.0 implementation currently present on the `develop` branch.

The roadmap and GitHub issues may describe later work. A planned API or behavior is not part of the current public contract until its implementation and tests are merged and the corresponding user documentation is updated.

## Main documents

| File | Purpose |
|---|---|
| [Main README](../README.md) | Project overview and current behavior |
| [INTRODUCTION.md](INTRODUCTION.md) | Scope and design goals |
| [BUILD.md](BUILD.md) | Current CMake prerequisites, options, build, and install behavior |
| [API_C.md](API_C.md) | Current C API behavior |
| [CPP.md](CPP.md) | Existing C++17 wrapper types and methods |
| [PORTING.md](PORTING.md) | Hooks and execution model used by the current ports |
| [TICK_SOURCE.md](TICK_SOURCE.md) | Internal versus external tick ownership |
| [SEMAPHORES.md](SEMAPHORES.md) | Binary and counting semaphore behavior |
| [MUTEXES.md](MUTEXES.md) | Mutex behavior and limitations |
| [QUEUES.md](QUEUES.md) | Queue behavior, storage, and ISR operations |
| [EXAMPLES_C.md](EXAMPLES_C.md) | Bundled example overview |
| [MODULE_STATUS.md](MODULE_STATUS.md) | Current source-module status |
| [TESTS_POSIX.md](TESTS_POSIX.md) | POSIX test-harness behavior and limits |
| [STATISTICS.md](STATISTICS.md) | Recorded STM32H755 timing measurements and their scope |
| [ROADMAP.md](ROADMAP.md) | Non-binding work planned toward 1.0.0 |
| [DOCUMENTATION.md](DOCUMENTATION.md) | Doxygen and documentation guidance |

## Current implemented synchronization surface

- binary and counting semaphores;
- owner-tracked non-recursive mutexes;
- fixed-size copy-based message queues.

The current release does not expose event flags, task notifications, IPC timeouts, or mutex priority inheritance.

## Accuracy rule

Where documentation and implementation differ, the source and tests describe what the current binary does. Such differences should be corrected in documentation immediately and tracked as implementation issues only when a different behavior has been deliberately selected for a later release.

The documentation does not claim complete automated synchronization with the source. Compile and link checks for documentation examples are tracked separately.