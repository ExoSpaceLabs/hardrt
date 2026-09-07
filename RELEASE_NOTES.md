# HardRT v0.5.1 Release Notes

HardRT v0.5.1 is a corrective patch for the hosted POSIX execution backend. The published v0.5.0 tag remains immutable. The patch completes the POSIX preemption work that was intended for v0.5.0 without changing the public C/C++ API, public synchronization-object layouts, or Cortex-M scheduling policy.

## Corrected

### Hosted POSIX preemption

The POSIX backend no longer uses `ucontext`. Each live HardRT application task executes in a pthread while the thread running `hrt_start()` remains the hosted scheduler/controller.

A monotonic timer pthread requests internal ticks. Targeted POSIX signals asynchronously park/resume the currently selected application pthread so a CPU-bound task that never calls a HardRT API cannot indefinitely prevent a scheduler-selected task from executing.

The common HardRT core remains authoritative for:

- READY/RUNNING/BLOCKED/SLEEP/EXITED state;
- priority and round-robin policy;
- wake/preemption decisions;
- sleeper/timeslice accounting;
- task lifecycle and slot ownership.

The hosted port performs execution transfer only; it does not duplicate scheduling policy.

### Tick serialization

`hrt_tick_from_isr()` now enters the existing port critical-section contract before it mutates common tick/scheduler state. This permits an application-owned external tick on the hosted POSIX port to serialize correctly with the scheduler/controller and task-side kernel paths.

On Cortex-M this nests through the existing BASEPRI-preserving critical-section implementation. It does not change the documented kernel-aware ISR priority ceiling or make higher-priority IRQs eligible to call HardRT APIs.

### Hosted regression coverage

The POSIX suite now contains an explicit asynchronous-preemption regression: a low-priority task spins indefinitely without entering a HardRT API while a higher-priority sleeping task must wake and execute. This directly covers the failure mode of the former cooperative hosted execution model.

## Package and build changes

- Project/package version is `0.5.1`.
- POSIX builds resolve CMake's `Threads` package.
- `HardRT::hardrt` exports `Threads::Threads` transitively for source-tree and installed-package consumers.
- Installed `HardRTConfig.cmake` resolves the Threads dependency before importing HardRT targets.
- The hosted POSIX port is documented and validated as a pthread-capable Linux environment rather than a generic timing model for all POSIX systems.
- A permanent `.github/workflows/release.yml` workflow now owns tag-driven release publication.
- Release builds validate `X.Y.Z` tag/version alignment and require `main`, `develop`, and the tag SHA to agree.
- Release publication builds POSIX and Cortex-M install trees, validates an installed POSIX consumer, creates reproducible archives, generates `SHA256SUMS`, extracts the matching section from `RELEASE_NOTES.md`, retains an Actions artifact, and publishes the GitHub Release.
- Linux CI still validates tag package assembly but no longer publishes release assets itself.

A consumer remains:

```cmake
find_package(HardRT 0.5 REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

No consumer-side pthread workaround is required.

## Compatibility

v0.5.1 is intended to be source/API and ABI compatible with v0.5.0 at the HardRT public interface:

- public C/C++ function signatures are unchanged;
- public synchronization-object layouts are unchanged;
- scheduler behavior documented for v0.5.0 remains the common-core contract;
- Cortex-M task-stack and context-switch semantics are unchanged.

Hosted POSIX runtime behavior is deliberately corrected. Code that depended on a CPU-bound hosted task retaining execution until it voluntarily entered HardRT was depending on a defect rather than a supported scheduler contract.

### Hosted stack semantics

The application-owned `stack_words` pointer and `n_words` count supplied to `hrt_create_task()` remain part of HardRT's task-lifetime and live-stack-overlap contract. On POSIX that storage is no longer the native execution stack: the pthread implementation owns a separate host stack. Native Cortex-M task stacks continue to use the application-provided storage directly.

### Process-level signal ownership

The v0.5.1 hosted backend currently reserves process-wide:

- `SIGALRM` for asynchronous task preemption/parking;
- `SIGUSR2` for hosted wake/resume handling.

Applications embedding the POSIX backend must not install incompatible handlers or use those signals for unrelated process-level protocols while HardRT is active. Configurable signal selection and handler restoration are follow-up host-integration improvements, not additions to the v0.5.1 public API.

### Host resource model

The hosted implementation consumes one pthread per live application task, the scheduler/controller thread that called `hrt_start()`, and one additional timer pthread when HardRT owns the internal tick. These resources are host-port implementation details and do not alter the static kernel storage or Cortex-M memory model.

## Validation gate

The v0.5.1 release is gated on:

- POSIX C/C++ builds across the supported Linux matrix;
- the complete hosted runtime suite, including asynchronous CPU-bound preemption;
- strict-warning + UBSan validation;
- installed CMake consumer validation with transitive thread linkage;
- bundled POSIX examples;
- null and Cortex-M contract builds;
- STM32H755 cross-build validation;
- documentation/API compile probes and Doxygen;
- a fresh full STM32H755 physical qualification run from the frozen release-candidate `develop` source.

The physical gate uses the canonical unfiltered runner:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

Release evidence requires board/OpenOCD probe PASS, 13/13 functional PASS, 38/38 benchmark PASS, and Overall PASS. The commit that generates that evidence is the hardware-qualified source SHA. Normally that SHA is promoted/tagged unchanged. A later release commit is permitted only for release automation/documentation, must pass `scripts/check_release_qualification_diff.py <qualified-sha> <release-sha>`, and must retain green hosted/cross-build/documentation CI. Any target/build-affecting post-qualification change requires a new full STM32H755 hardware run.

The existing v0.5.0 physical STM32 timing campaign remains historical evidence. It is not reinterpreted or reused as v0.5.1 physical qualification.

---

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
- Documentation compile/link-drift checks, repository-wide tracked-Markdown link/path validation, and Doxygen CI.
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
- Task-control and blocking semaphore/queue calls now validate that a current RUNNING application task exists even in non-debug builds; rejected no-current/non-running calls do not consume semaphore tokens or modify queue contents.

### Cortex-M contract

The Cortex-M port now has explicit/validated behavior for:

- FPU context preservation on the supported hard-float path;
- BASEPRI-preserving nested critical sections;
- external-tick ownership/startup;
- PendSV scheduler/context transitions;
- semaphore, queue, event, and notification ISR wake paths.

### CMake package matching

The generated package-version file now uses `SameMinorVersion` for the pre-1.0 line. A v0.5.x package may satisfy a compatible v0.5 request, but v0.5.0 will not silently satisfy a consumer that requested HardRT 0.4.x. This matches the documented policy that pre-1.0 minor releases may intentionally change source behavior or public layouts.

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
8. CMake consumers pinned to the 0.4 minor line must explicitly migrate their package requirement to 0.5 after reviewing these changes.

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
