# HardRT v0.5.0 Release Notes

HardRT v0.5.0 is a pre-1.0 minor release focused on scheduler/lifecycle correctness, deterministic wake behavior, Cortex-M qualification, and the new event-flag/task-notification synchronization surface.

## Added

- Statically allocated 32-bit event flags with wait-any, wait-all, retained-bit, and clear-on-exit semantics.
- Event task-context and ISR set/clear operations with bounded waiter inspection.
- One private 32-bit notification value plus pending/wait state per application task.
- Notification set-bits, overwrite, no-overwrite, and saturating increment actions.
- Notification wait/take operations and ISR producer support.
- Allocation-free C++17 `EventFlags`, `TaskNotification`, and typed `NotifyAction` wrappers.
- C and C++ event/notification examples.
- Explicit public lifecycle/configuration status handling.
- Runtime task creation with safe EXITED-slot reclamation and live-stack overlap rejection.
- Static intrusive READY and sleeper structures with explicit task/slot-state invariants.
- STM32H755 event/notification functional validation and DWT timing/profiling cases.
- Deterministic hosted event/notification stress coverage under priority, global RR, and priority-RR policies.
- Documentation compile/link-drift checks, repository-local link/path validation, and Doxygen CI.
- Explicit pre-1.0 compatibility/versioning policy in `docs/COMPATIBILITY.md`.

## Changed

### Global round-robin

`HRT_SCHED_RR` is now true global round-robin. All READY application tasks participate in one FIFO regardless of priority value. A newly awakened task joins the tail and does not steal the running task's unused quantum.

### `hrt_sleep(0)`

`hrt_sleep(0)` is now an immediate scheduling point. It does not enter SLEEP, join the sleeper queue, or wait for a tick. Use positive durations when an actual delay is required.

### Wake and ISR semantics

`need_switch` is scheduler-aware. Waking a waiter does not automatically imply immediate preemption. Under priority-based policies, the woken task must outrank the current task; global RR preserves the current task's queue/quantum semantics.

ISR producers pend a context switch through the port mechanism rather than executing a task directly from the ISR.

### Lifecycle and task state

- Kernel lifecycle is explicitly `UNINITIALIZED -> INITIALIZED -> RUNNING`.
- Invalid repeated initialization/start requests are rejected.
- RUNNING is distinct from READY.
- TCB-slot ownership is distinct from task execution state.
- A returned/deleted task enters EXITED and may later have its slot reclaimed.
- Live task stacks may not overlap.
- Runtime task creation is supported after scheduler start and participates at the next scheduling point.

### Cortex-M contract

The Cortex-M port now has explicit/validated behavior for:

- FPU context preservation on the supported hard-float path;
- BASEPRI-preserving nested critical sections;
- external-tick ownership/startup;
- PendSV scheduler/context transitions;
- semaphore, queue, event, and notification ISR wake paths.

## Event-flag semantics

- Public bit width is `uint32_t`.
- A zero wait mask is invalid.
- Wait-any is the default; wait-all requires every requested bit.
- When one update satisfies multiple waiters, all are evaluated against the same post-set snapshot.
- Clear-on-exit is applied only after that common scan; one waiter cannot hide a bit from another waiter already satisfied by the same update.
- Waiter publication order is FIFO; the active scheduler policy determines execution order.
- Event-set work is bounded by configured application-task capacity.

## Task-notification semantics

- Notifications are per-task and require no separate synchronization object.
- A notification sent before the target waits remains pending.
- A notification wakes only a task blocked specifically on its notification; it does not wake unrelated semaphore/queue/mutex/event/sleep blocking.
- Increment saturates at `UINT32_MAX`.
- Idle, unused, invalid, and EXITED targets are rejected.
- Producer work is O(1).

## Migration from v0.4.0

Applications moving from v0.4.0 should review these behavior changes:

1. Code that relied on priority-sensitive `HRT_SCHED_RR` ordering must be updated for one global FIFO.
2. Code that used `hrt_sleep(0)` as a one-tick delay must use a positive delay instead.
3. ISR code must interpret `need_switch` as a scheduler decision rather than simply “a waiter woke”.
4. Initialization/start failures now have an explicit public status contract.
5. Applications must not reuse or partially overlap a live task stack.
6. A task must still release owned mutexes before return/deletion; automatic mutex owner-death recovery is not provided in v0.5.0.
7. Consumers should rebuild HardRT and application objects together because public concrete synchronization-object layouts are not ABI-stable across pre-1.0 minor releases.

## Compatibility

v0.5.0 does **not** claim ABI compatibility with v0.4.0. Source, behavioral, ABI, and CMake-package compatibility are defined separately in `docs/COMPATIBILITY.md`.

Canonical package targets remain:

```cmake
HardRT::hardrt
HardRT::hardrtpp
```

The second target is present when the optional C++ wrapper is enabled.

## Validation

Hosted CI covers Linux/POSIX behavior, strict-warning + UBSan signal stress, C/C++ examples, installed-package consumers, documentation-originated API compile probes, Doxygen generation, Cortex-M cross-builds, and STM32H755 validation firmware cross-builds.

The release-grade physical gate uses exactly one command on a NUCLEO-H755ZI-Q CM7 target:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The v0.5 matrix contains 13 functional contracts and 38 benchmark images, including event/notification ISR-to-task latency, notification producer costs, and event waiter-scan scaling at 1, 8, 16, and 32 registered waiters.

Timing results are configuration-specific engineering measurements. Measured maxima are not universal WCET proofs.

## Deferred beyond v0.5.0

- Generic IPC timeout variants.
- Mutex priority inheritance/ceiling and owner-death recovery.
- Full analytical critical-section/WCET bounds and richer interference matrices.
- Queue-copy scaling bounds suitable for a formal hard-real-time profile.
- Tickless idle and high-resolution timer facilities.
