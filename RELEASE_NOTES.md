# HardRT v0.4.0 Release Notes

## Added

- Fixed-size, copy-based message queues with blocking, non-blocking, and ISR operations.
- Owner-tracked, non-recursive mutexes with FIFO waiter queues and direct handoff.
- C and C++ mutex examples under `examples/mutex_basic` and `examples/mutex_basic_cpp`.
- `scripts/run-all-examples.sh` for POSIX-compatible example verification.
- `hrt_task_delete()` for explicit task removal; task trampolines also call it when a task returns.
- `hrt_now_ms()` for millisecond conversion from the configured tick rate.
- Expanded POSIX tests for queues, mutexes, task return, deletion, external tick mode, and time helpers.
- Optional header-only C++17 wrapper in `cpp/hardrtpp.hpp`.
- C++ examples for tasks, semaphores, counting semaphores, mutexes, and queues.

## Current behavior and limitations

- The kernel core remains C and uses static allocation.
- The C++ wrapper is optional at the API level, although the current root CMake project enables the C++ language during every configure.
- Mutexes are task-context-only and do not implement priority inheritance or timed lock.
- Blocking semaphore, mutex, and queue operations have no timeout.
- The POSIX port uses Linux/glibc `ucontext` and cooperative context handoff at HardRT scheduling points.
- The current `HRT_SCHED_RR` implementation still selects from priority queues; it is not global priority-independent round-robin.
- `hrt_sleep(0)` sleeps for one tick in v0.4.0. Use `hrt_yield()` for an immediate scheduling point.
- ISR `need_switch` outputs report that a waiter was awakened; they do not currently perform a higher-priority comparison.

## Compatibility

HardRT v0.4.0 does **not** make an ABI-compatibility guarantee with v0.3.x.

Public synchronization structures are concrete types. In particular, the semaphore structure changed to include counting-semaphore state, which can change size and member offsets for already compiled consumers. Applications should rebuild against the v0.4.0 headers and library together.

Source compatibility is best effort for the documented application-facing APIs, but internal names and public structure layouts are not frozen before 1.0.0.

## Validation scope

The release has been exercised on Ubuntu 22.04, Ubuntu 24.04, GitHub-hosted Linux environments, and STM32H755ZI-Q Cortex-M7 validation setups.

A POSIX test-suite failure has been observed on Debian 13 with GCC 14 and glibc 2.41. The POSIX port is intended for logic testing and is not claimed to support every libc or Unix platform.

Timing values recorded in `docs/STATISTICS.md` apply to the documented hardware, build, instrumentation, and workload. They are measurements rather than universal worst-case latency guarantees.