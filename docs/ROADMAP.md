# 🧭 HardRT Roadmap

This roadmap outlines the evolution of HardRT toward the planned 1.0.0 release.

> Roadmap items may shift as implementation priorities change.

## 🎯 Hard real-time direction

HardRT is intended to evolve into a small Cortex-M RTOS with documented hard real-time properties, not merely a lightweight scheduler with real-time terminology.

The project therefore prefers designs that are statically bounded, analyzable and reproducible over designs that are merely convenient or fast on average.

The hard real-time direction means:

- no dynamic allocation in kernel runtime paths;
- bounded kernel data structures and explicitly bounded iteration counts;
- bounded critical sections with documented interrupt-priority assumptions;
- deterministic scheduler, wake-up, timeout and ISR semantics;
- explicit priority-inversion handling for shared resources;
- defined execution-time and memory costs for kernel primitives;
- reproducible Cortex-M timing measurements tied to exact builds and hardware configurations;
- clear separation between measured maxima and proven/configuration-specific upper bounds;
- no hard-real-time claim for the POSIX port, which remains a functional and scheduler-validation environment.

Hard real-time qualification is progressive. Public documentation must not turn development measurements into unconditional WCET claims.

## ✅ Completed foundations

- Core scheduler with static tasks
- Null and POSIX ports
- Cortex-M port foundation
- Binary and counting semaphores
- Owner-tracked mutex primitive
- Fixed-size message queues
- Version and port metadata via CMake
- C and C++ example set for tasks and current IPC primitives
- POSIX test harness expansion
- Installed-package C and C++ consumer validation

## ✅ v0.5 scheduler/lifecycle hardening complete

The v0.5 development line now has an accepted scheduler/lifecycle baseline with:

- authoritative private core/port interface;
- kernel internals removed from installed public headers;
- explicit `UNINITIALIZED -> INITIALIZED -> RUNNING` lifecycle;
- distinct task execution state and TCB-slot ownership;
- READY, RUNNING, SLEEP, BLOCKED and EXITED task states;
- intrusive policy-specific READY queues with duplicate-membership protection;
- true global `HRT_SCHED_RR`;
- retained-quantum `HRT_SCHED_PRIORITY_RR` behavior;
- static intrusive delta sleeper queue;
- scheduler-aware task/ISR wake-preemption decisions;
- application-task capacity separated from the private idle slot;
- Cortex-M hard-float context preservation;
- BASEPRI-preserving critical sections;
- external-tick startup contract;
- transactional and runtime-safe task creation;
- task-stack overlap/reuse protection.

Physical NUCLEO-H755ZI-Q development qualification on exact SHA `80f2042f2c64053a9ea888666474c5dad5f72797` passed:

```text
11 / 11 functional contracts
22 / 22 hardware benchmarks
overall PASS
```

This completes the scheduler/lifecycle hardening phase. The final v0.5.0 RC must still repeat the complete hardware run on the exact frozen release SHA.

## 🚧 Remaining v0.5 feature/release work

The current v0.5 critical path is intentionally narrow:

- event flags and per-task notifications (#38–#44);
- documentation-example/API-drift CI (#35);
- explicit pre-1.0 compatibility/versioning policy (#36);
- release metadata, notes, migration guidance and final packaging (#45);
- one final unfiltered STM32H755 qualification on the frozen v0.5.0 RC SHA (#56).

The broader hard-real-time qualification backlog below remains important, but is **not a scheduler-hardening blocker for v0.5.0** as long as release documentation avoids unsupported universal latency guarantees.

## 🧱 Post-v0.5 synchronization hardening

- IPC timeout variants (`hrt_sem_take_timeout`, queue timeout variants, mutex timed lock if adopted)
- Priority inheritance or another bounded priority-inversion mitigation strategy for mutexes
- Robust/owner-death mutex semantics if adopted (#66)
- Strict queue reservation/direct-handoff fairness if a later queue redesign requires it

## 🕒 Post-v0.5 timing and hard-real-time qualification

- Maximum observed and structurally bounded critical-section characterization
- Queue-copy scaling and a decision on maximum item size versus pointer/index-oriented designs
- More complete interrupt-interference and task-contention matrices
- True event-to-ISR-entry measurements where hardware permits
- Machine-readable timing output with complete build/hardware metadata
- Regression thresholds only after run-to-run variance is understood
- `hrt_delay_until()` / periodic-task API with bounded release behavior
- Tickless idle only if timing and wake behavior remain explicitly bounded
- High-resolution timers only with a defined deterministic contract

These items are tracked primarily by #37 and #48 with #49–#54. They remain open as 1.0-quality work rather than being silently declared solved.

## 🧩 Broader platform work

- CM4↔CM7 communication primitives (AMP)
- Shared-memory mailbox interface
- More Cortex-M targets and production qualification configurations

## 🧪 Verification work

- Compile documentation examples and validate referenced commands/links
- Static analysis and MISRA-oriented cleanup
- Optional stack canary/high-watermark diagnostics
- Static-memory accounting after events/notifications finalize the v0.5 TCB/object footprint
- Additional deterministic stress/invariant testing for new synchronization primitives

## 🏁 1.0.0 target themes

- Verified Cortex-M configurations with explicit assumptions
- Deterministic scheduler with clear behavioral guarantees
- Bounded kernel operations suitable for worst-case analysis
- Defined priority-inversion strategy
- Complete IPC suite: semaphores, mutexes, queues, events
- Timing primitives suitable for periodic real-time tasks
- Reproducible evidence for advertised latency bounds
- Documentation and public API freeze
