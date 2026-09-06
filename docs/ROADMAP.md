# HardRT Roadmap

HardRT is evolving toward a small Cortex-M RTOS with explicitly documented hard-real-time properties. The project prefers statically bounded, analyzable behavior over convenience that is difficult to reason about.

## Hard real-time direction

The long-term qualification model requires:

- no dynamic allocation in kernel runtime paths;
- bounded kernel structures and iteration counts;
- bounded critical sections with explicit interrupt-priority assumptions;
- deterministic scheduler, wake, timeout and ISR semantics;
- a bounded priority-inversion strategy;
- defined execution-time and memory costs for kernel primitives;
- reproducible Cortex-M measurements tied to exact builds/hardware;
- clear separation between measured maxima and analytical/WCET bounds;
- no timing claim for the POSIX functional/scheduler port.

## Completed foundations

- static task/kernel core;
- null, POSIX and Cortex-M ports;
- binary/counting semaphores;
- owner-tracked mutexes;
- fixed-capacity message queues;
- fixed-priority, global RR, and priority-RR scheduling;
- installed CMake package and optional C++17 wrappers;
- hosted test suite and installed consumer checks.

## v0.5.0 implementation complete

The v0.5 line includes:

- explicit `UNINITIALIZED -> INITIALIZED -> RUNNING` lifecycle;
- separate slot ownership and task execution state;
- READY/RUNNING/SLEEP/BLOCKED/EXITED task states;
- intrusive policy-specific READY storage with duplicate protection;
- true global `HRT_SCHED_RR`;
- retained-quantum `HRT_SCHED_PRIORITY_RR`;
- static intrusive delta sleeper queue;
- scheduler-aware task/ISR wake decisions;
- application-task capacity separated from private idle;
- Cortex-M hard-float context preservation;
- BASEPRI-preserving critical sections;
- external-tick startup/ownership contract;
- transactional/runtime-safe task creation and stack overlap protection;
- 32-bit shared event flags;
- per-task 32-bit notifications;
- task/ISR event and notification producers;
- C/C++ event/notification wrappers/examples;
- deterministic event/notification stress/invariant coverage;
- STM32H755 signal timing/profiling fixtures integrated into the single manual runner;
- explicit pre-1.0 compatibility/versioning policy;
- documentation-originated C/C++ compile probes, link/path validation, and Doxygen CI;
- project/release metadata for 0.5.0.

The v0.5 physical release matrix is **13 functional contracts + 38 benchmark images**.

## v0.5.0 release procedure

`develop` is the only release-candidate branch. Temporary feature/release branches must be merged by PR and removed before release qualification.

The release procedure preserves one exact qualified source commit:

1. merge all release-facing source, documentation, version and package changes into `develop`;
2. require Linux, strict/UBSan signal stress, Documentation, C/C++ example/package, Cortex-M, and STM32 cross-build jobs to pass on one frozen `develop` commit;
3. run `scripts/stm32_manual_test_full.sh` unfiltered on that exact `develop` commit and require board probe + 13/13 functional + 38/38 benchmark PASS;
4. retain the generated qualification package outside the tracked source tree and publish it as a release artifact;
5. fast-forward `main` to the same qualified `develop` commit without changing its tree;
6. tag `0.5.0` on `main`, publish release artifacts, and leave only the long-lived `main` and `develop` branches.

No tracked source or documentation change belongs between final physical qualification and the release tag unless the new `develop` SHA is deliberately requalified.

## Post-v0.5 synchronization work

- generic IPC timeout variants;
- priority inheritance/ceiling or another bounded mutex inversion strategy;
- robust/owner-death mutex semantics if adopted (#66);
- any later queue fairness/reservation redesign justified by requirements.

## Post-v0.5 timing and hard-real-time qualification

- analytical/maximum critical-section bounds;
- queue-copy scaling and explicit item-size/design limits;
- richer interrupt/task interference matrices;
- true hardware event-to-ISR-entry measurement where practical;
- machine-readable timing evidence with complete build/hardware metadata;
- regression thresholds after run-to-run variance is characterized;
- absolute periodic timing (`hrt_delay_until()` or equivalent) and release-jitter qualification;
- tickless idle/high-resolution timers only under an explicit bounded contract.

These remain tracked primarily by #37, #48, and #49–#54 and are 1.0-quality work rather than hidden v0.5 blockers.

## Broader platform work

- CM4↔CM7/AMP communication primitives;
- shared-memory mailbox facilities;
- additional Cortex-M targets and production qualification profiles.

## Verification toward 1.0

- static analysis and MISRA-oriented cleanup;
- optional stack canary/high-watermark diagnostics;
- complete static-memory accounting for supported target profiles;
- stronger machine-readable qualification/evidence tooling.

## 1.0.0 themes

- verified Cortex-M configurations with explicit assumptions;
- deterministic scheduler and synchronization guarantees;
- bounded kernel operations suitable for worst-case analysis;
- defined priority-inversion strategy;
- complete synchronization/time primitives for intended use cases;
- reproducible evidence for advertised latency bounds;
- stable documented public API boundary.
