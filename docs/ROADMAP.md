# HardRT Roadmap

HardRT is being developed toward a **small, explicitly qualified hard real-time RTOS for Cortex-M**, not merely a lightweight preemptive scheduler with real-time terminology.

The project therefore prefers statically bounded, analyzable and reproducible behavior over convenience that is difficult to reason about. Features and ports are valuable only when they preserve or strengthen that contract.

## Hard real-time direction

For explicitly supported Cortex-M configurations, the long-term qualification model requires:

- no dynamic allocation in kernel runtime paths;
- bounded kernel structures and explicitly bounded iteration counts;
- bounded critical sections with documented interrupt-priority assumptions;
- deterministic scheduler, wake, timeout and ISR semantics;
- bounded priority inversion for shared resources;
- periodic timing primitives suitable for schedulability analysis;
- defined execution-time and memory costs for kernel primitives;
- reproducible Cortex-M measurements tied to exact source, toolchain and hardware configuration;
- complete clock/cache/FPU/memory/IRQ metadata for timing evidence;
- clear separation between measured averages/maxima and analytical or configuration-specific upper bounds;
- no hard-real-time timing claim for the POSIX port, which remains a functional/scheduler-validation environment.

Hard real-time qualification is progressive. A release may establish deterministic semantics needed for later proof without claiming that every WCET or interference bound is already complete.

## Completed v0.5.x foundations

The v0.5 line established the scheduler, lifecycle and synchronization foundation needed before deeper timing guarantees are credible:

- static task/kernel core with no dynamic allocation in runtime paths;
- explicit `UNINITIALIZED -> INITIALIZED -> RUNNING` lifecycle;
- separate slot ownership and READY/RUNNING/SLEEP/BLOCKED/EXITED task state;
- intrusive bounded READY storage with duplicate protection;
- true global `HRT_SCHED_RR` and retained-quantum `HRT_SCHED_PRIORITY_RR`;
- static intrusive delta sleeper queue;
- scheduler-aware task/ISR wake decisions;
- application-task capacity separated from the private idle task;
- Cortex-M hard-float context preservation;
- BASEPRI-preserving critical sections and documented kernel-aware IRQ ceiling;
- internal and application-owned external tick paths;
- task-stack overlap and EXITED-slot reclamation safety;
- semaphores, owner-tracked mutexes and fixed-capacity message queues;
- 32-bit shared event flags and per-task 32-bit notifications;
- task/ISR event and notification producers;
- deterministic hosted stress/invariant coverage;
- STM32H755 physical functional and timing qualification infrastructure;
- installed CMake package and optional C++17 wrappers;
- explicit pre-1.0 compatibility/versioning policy.

v0.5.1 additionally replaced the hosted POSIX `ucontext` execution model with pthread-backed scheduler-controlled execution so CPU-bound hosted tasks can be asynchronously preempted while the common HardRT core remains authoritative for scheduling semantics.

The v0.5 physical qualification line reached **13 functional contracts + 38 benchmark images** on the NUCLEO-H755ZI-Q CM7 reference target.

### v0.5.1 release housekeeping

The release itself is published. Remaining release-record cleanup is tracked by #82, including attachment of retained physical qualification evidence and deletion of temporary release/fix branches. This housekeeping should be closed before accumulating new release debris, because apparently branches reproduce when nobody is watching.

## Release progression to 1.0

### 0.6.x — bounded synchronization and periodic timing

Release tracker: #91.

The objective is to close the most important semantic gaps that prevent useful bounded blocking and periodic-task analysis.

Primary work:

- common wrap-safe, statically bounded IPC timeout infrastructure (#68);
- bounded mutex priority-inversion strategy, using priority inheritance, priority ceiling or another explicitly bounded model selected by analysis (#89);
- absolute periodic timing through `hrt_delay_until()` or equivalent, including deadline/tick-wrap and missed-release semantics (#90);
- critical-section characterization needed to qualify synchronization behavior (#53);
- queue-copy scaling measurement and an explicit queue item-size/design bound decision (#53, supported by #52);
- initial periodic-release timing evidence tied to the new absolute timing primitive (#54);
- full hosted/cross-build regression and unfiltered STM32H755 physical qualification on the frozen 0.6 release candidate.

0.6 establishes the semantics required for deeper timing proof. It must not present measured maxima as universal WCET guarantees.

Explicitly deferred from the 0.6 critical path:

- robust/owner-death mutex recovery (#66);
- architecture-neutral public stack API cleanup (#76);
- public diagnostic namespace cleanup (#69);
- AVR/MSP430 and broader architecture ports (#77/#78);
- Raspberry Pi 5 hosted/bare-metal expansion (#79/#80);
- optional SVC/MPU privilege-boundary architecture (#87);
- the complete formal interference/WCET qualification matrix from #37/#48/#49-#54.

### 0.7.x — hard-real-time timing model and interference qualification

The objective is to turn deterministic 0.5/0.6 semantics into reproducible configuration-specific timing evidence and analytical bounds where justified.

Primary work:

- scheduler decision and context-switch decomposition (#50);
- true hardware event/ISR/task response decomposition (#51);
- IPC fast-path, contention and handoff timing depth (#52);
- complete critical-section and bounded-operation timing (#53);
- tick, deadline-expiry and periodic release-jitter characterization (#54);
- representative higher-priority task and IRQ interference matrices (#37/#51/#54);
- complete build/compiler/link, clock-tree, cache, FPU, memory-placement and IRQ-priority metadata (#37/#49);
- machine-readable timing output suitable for automated comparison (#37/#49);
- regression thresholds only after normal run-to-run variance is understood;
- convert measurements into explicit configuration-specific upper-bound models where the evidence and architecture permit it.

The target is not a single attractive latency number. The target is a timing model that tells an application engineer what assumptions are required to reason about worst-case response.

### 0.8.x — pre-1 kernel/API hardening and portability cleanup

The objective is to remove remaining pre-1 API/architecture debt before the public contract is frozen.

Primary work:

- architecture-neutral task-stack storage contract (#76);
- namespace public diagnostic identifiers (#69);
- complete static-memory accounting for supported target profiles;
- optional stack canary/high-watermark diagnostics where they remain deterministic and zero-cost when disabled;
- static analysis and MISRA-oriented cleanup;
- installed API/header audit for collision, ABI and portability hazards;
- additional Cortex-M target/profile work where it strengthens the qualification model;
- first non-ARM portability work only when the common kernel contract is ready to benefit from it.

SVC/MPU-backed privilege separation remains an optional future direction under #87. SVC must not replace PendSV as the Cortex-M context-switch mechanism, and it is not required merely because other RTOSes use it.

### 0.9.x — frozen 1.0 release candidate

The objective is to stop changing fundamentals and prove the intended stable contract.

Primary work:

- freeze the 1.0 public C API and supported C++ wrapper contract;
- freeze the explicitly supported Cortex-M target/configuration profiles;
- close any remaining hard-real-time semantic gaps;
- run the complete functional, synchronization, timing and interference qualification matrix against exact frozen source;
- establish justified regression thresholds;
- verify complete static-memory accounting;
- package machine-readable and human-readable qualification evidence;
- audit every public hard-real-time claim against actual evidence;
- leave no known incompatibility or timing assumption hidden in implementation folklore.

### 1.0.0 — explicitly qualified HardRT contract

HardRT 1.0 means **qualified hard-real-time behavior for explicitly supported Cortex-M configurations**, not simply that enough features accumulated to justify a large version number.

For each supported configuration, HardRT should be able to state and support:

- deterministic scheduler and wake/preemption behavior;
- bounded kernel data structures and iteration counts;
- bounded synchronization and timeout semantics;
- bounded mutex-induced priority inversion;
- bounded critical sections with explicit interrupt-priority assumptions;
- periodic timing primitives suitable for schedulability analysis;
- defined execution-time and memory costs for kernel primitives;
- configuration-specific timing and latency bounds with clearly stated assumptions;
- reproducible qualification evidence tied to exact source, toolchain, hardware, clock, cache, FPU, memory placement and IRQ configuration;
- stable documented public API/ABI boundary for the supported 1.x contract.

The qualification umbrella is #48. Timing/evidence work is primarily tracked by #37 and #49-#54.

## Broader platform work

Platform expansion remains useful, but it is subordinate to the hard-real-time contract rather than a substitute for it.

Tracked candidates include:

- architecture-neutral stack prerequisite (#76);
- AVR/ATmega328P portability target (#77);
- constrained MSP430G2553 portability target (#78);
- Raspberry Pi 5 POSIX integration/soak target (#79);
- bare-metal AArch64/Raspberry Pi 5 feasibility investigation (#80);
- CM4<->CM7/AMP communication primitives;
- shared-memory mailbox facilities;
- additional Cortex-M production qualification profiles.

A new architecture compiling successfully is portability evidence. It is not automatically hard-real-time qualification evidence.

## Verification principles toward 1.0

Every release on the path to 1.0 should preserve these rules:

- functional behavior is tested independently of timing claims;
- Cortex-M physical evidence is tied to exact source and hardware configuration;
- POSIX remains a logic/scheduler validation port, not a timing oracle;
- measured averages, observed maxima and analytical/WCET bounds are labeled distinctly;
- changes that alter a bounded operation require the affected timing/resource evidence to be reconsidered;
- disabled diagnostics/instrumentation should remain zero-cost where that is part of their contract;
- release qualification is performed on frozen source and retained as reproducible evidence;
- temporary release branches are removed after release so `main` and `develop` remain the long-lived branches.
