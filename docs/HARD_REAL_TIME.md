# Hard real-time qualification

HardRT is being engineered toward documented, explicitly qualified **hard real-time behavior on supported Cortex-M configurations**. This page defines that engineering boundary; it is not a claim that every build or every current pre-1 release is already hard real time.

The intended 1.0 outcome is stronger than "preemptive RTOS" or "low latency": a supported configuration must have deterministic scheduler/synchronization semantics, statically bounded kernel behavior, bounded blocking and priority inversion, bounded critical sections under explicit IRQ assumptions, periodic timing suitable for schedulability analysis, and reproducible evidence for every advertised timing bound.

## Qualification boundary

Hard-real-time qualification applies only to Cortex-M targets/configurations whose timing and interference assumptions are recorded. The POSIX port is a functional/scheduler validation environment and is excluded from timing qualification.

A successful build or functional test on another architecture is portability evidence, not automatically hard-real-time qualification evidence.

SVC/MPU privilege separation is an optional future architecture direction under #87. SVC is not required for the existing scheduler and, if introduced, must not replace PendSV as the Cortex-M context-switch mechanism.

## v0.5 development evidence

The scheduler/lifecycle hardening baseline on NUCLEO-H755ZI-Q / CM7 passed 11 functional contracts and 22 historical benchmark images on SHA `80f2042f2c64053a9ea888666474c5dad5f72797`.

After events/notifications landed, development hardware run `20260905T152422Z_6f4ef62a` passed:

```text
HardRT SHA:   6f4ef62a8a0d13a0632537c6e65a50cbd315d656
Functional:   13 / 13 PASS
Historical benchmarks: 22 / 22 PASS
Overall:      PASS
```

The final v0.5 runner additionally integrates 16 event/notification timing images, for a complete release matrix of **13 functional contracts + 38 benchmark images**. Final release evidence must come from the exact frozen release SHA.

v0.5.1 additionally corrects the hosted POSIX execution model while preserving the Cortex-M scheduler/context contract. POSIX remains excluded from hard-real-time timing claims.

These results are development/release measurements, not a universal WCET proof.

## Required properties for a formal hard-real-time configuration

A HardRT configuration can be described as hard real time only when relevant kernel operations and interference sources have finite, documented bounds under explicit assumptions.

### Static resource bounds

- no dynamic allocation in kernel runtime paths;
- fixed task, waiter, event, notification, and queue capacities;
- documented stack/static-memory costs;
- no unbounded hidden timer or worker infrastructure.

### Bounded scheduler behavior

- deterministic READY selection and insertion/removal;
- explicit behavior for every scheduler policy;
- no duplicate/stale scheduler membership;
- bounded sleeper/deadline insertion and expiry processing;
- bounded scheduler-side reactions to timeout, wake and priority changes.

### Bounded interrupt behavior

- documented interrupt-priority ceiling for kernel-aware ISRs;
- bounded critical sections;
- no ISR blocking;
- explicit scheduler decision after ISR wake;
- documented higher-priority interrupt interference assumptions;
- explicit separation between interrupt entry latency, kernel ISR service cost, PendSV delay and resumed-task latency.

### Bounded synchronization behavior

- deterministic waiter ordering/handoff;
- bounded event waiter inspection;
- O(1) notification producer work;
- deterministic, bounded timeout bookkeeping for blocking APIs (#68);
- explicit bounded priority-inversion strategy for mutexes (#89);
- finite configuration-specific mutex blocking analysis;
- queue-copy cost bounded by an explicit item-size/design contract rather than an unspecified application payload size.

### Periodic timing behavior

- a stable-phase absolute periodic release primitive such as `hrt_delay_until()` (#90);
- the phase reference is established once, at the first task instance, and future nominal releases are derived from that persistent reference rather than from actual completion or wake time;
- for nominal release `R`, period `P`, and current time `now`, the remaining wait is conceptually `P - (now - R)` when positive, equivalently `next_release - now`;
- the elapsed interval intentionally includes application execution, preemption by other tasks, interrupt service, scheduler work, and context-switch overhead, so those costs consume available slack instead of shifting the next nominal release later;
- wrap-safe deadline semantics;
- documented missed-deadline/overrun behavior that does not silently rebase the periodic phase to `now`;
- no cumulative phase drift caused by repeated relative sleeps or by rebasing each cycle;
- bounded expiry/release processing;
- measured and eventually bounded release jitter under stated interference assumptions (#54).

A 10 ms task period illustrates the required semantics. If execution and interference consume 4 ms after the nominal release, only 6 ms remain to wait. If they consume 13 ms, the task is already 3 ms late; it must follow the documented overrun policy rather than receiving a fresh 10 ms delay.

### Lifecycle/error determinism

- invalid configuration/lifecycle transitions fail predictably;
- contract violations are observable;
- debug/release differences do not silently change public behavior;
- task exit cannot silently invalidate mutex or waiter invariants.

### Reproducible timing evidence

Formal claims require, as applicable:

- exact HardRT commit;
- compiler/binutils versions and compile/link flags;
- MCU/core and board revision;
- clock tree and flash/SRAM placement;
- cache/FPU/lazy-stacking state;
- tick source/frequency;
- IRQ priorities and kernel-aware interrupt ceiling;
- task priorities and scheduler policy;
- configured task/waiter/queue capacities;
- queue item sizes and other data-dependent operation limits;
- relevant interference workload;
- raw/machine-readable timing evidence.

## What v0.5 establishes

v0.5 provides engineering evidence for:

- static kernel/task/synchronization storage;
- deterministic policy-specific READY storage;
- true global RR and retained-quantum priority RR;
- intrusive delta sleeper queue with O(1) no-expiry tick work and O(K) work for K expiries;
- scheduler-aware task/ISR wake decisions;
- RUNNING/READY and slot/task-state separation;
- Cortex-M hard-float context preservation;
- BASEPRI-preserving nested critical sections;
- external-tick ownership/startup ordering;
- semaphore/queue/mutex/event/notification hardware behavior;
- event ISR-to-task and waiter-scan profiling fixtures;
- notification ISR-to-task and producer-cost profiling fixtures.

Event-set cost is bounded by configured application-task capacity because the set path scans registered waiter metadata. Task-notification producer cost is O(1). The physical profile reports observed cycles for representative/fan-out cases; those measurements are not analytical upper bounds.

## Qualification progression to 1.0

### 0.6.x — establish bounded synchronization and periodic semantics

Tracked by #91.

0.6 must establish the semantic foundations needed before deeper timing proof is meaningful:

- bounded common IPC timeout model (#68);
- bounded mutex priority-inversion model (#89);
- stable-phase absolute periodic release timing (#90);
- synchronization-critical critical-section measurement (#53);
- queue-copy scaling and explicit design/qualification bound (#53/#52);
- initial physical periodic-release evidence (#54).

The periodic mechanism must prove that application execution, task preemption, interrupt handling, scheduler latency, and context-switch overhead are accounted as elapsed time inside the current period. They reduce the remaining delay but do not redefine the next nominal release.

0.6 is allowed to say these operations are structurally/deterministically bounded where that has been demonstrated. It must not promote observed maxima into universal WCET claims.

### 0.7.x — characterize timing and interference

0.7 should deepen the timing model through #37 and #49-#54:

- scheduler/context-switch decomposition;
- hardware event -> ISR -> READY -> PendSV -> task decomposition;
- synchronization fast-path/contention/handoff cost;
- maximum critical-section characterization;
- periodic release jitter and phase error relative to the persistent nominal schedule;
- higher-priority task/IRQ interference;
- complete machine-readable evidence metadata;
- configuration-specific analytical upper bounds where justified.

The objective is not a single benchmark number. It is a usable response-time model for application schedulability reasoning.

### 0.8.x — harden the pre-1 kernel/API boundary

0.8 should remove remaining portability/API debt before freeze:

- architecture-neutral task-stack contract (#76);
- public diagnostic identifier namespacing (#69);
- complete static-memory accounting;
- deterministic stack diagnostics where adopted;
- static-analysis/MISRA-oriented cleanup;
- installed API/header/ABI audit;
- broader target portability only where it strengthens the common kernel contract.

### 0.9.x — freeze and qualify the 1.0 candidate

0.9 should freeze the intended stable API and supported Cortex-M configuration(s), then run the complete functional/timing/interference qualification campaign against exact source. No unresolved hard-real-time semantic hole should be knowingly carried into 1.0.

### 1.0.0 — qualified hard-real-time contract

For every supported Cortex-M configuration, documentation must identify:

- finite structural bounds for kernel data structures and loops;
- synchronization/blocking bounds including mutex priority inversion;
- critical-section/IRQ assumptions;
- stable-phase periodic release semantics and jitter assumptions;
- execution-time and memory costs used by schedulability analysis;
- measured evidence and analytical bounds, clearly distinguished;
- exact source/toolchain/hardware/runtime configuration supporting each guarantee.

1.0 therefore means **supported hard-real-time configurations are explicitly qualified**, not merely that the feature checklist is long enough.

## Remaining 1.0-quality work

The major open work is tracked by:

- #68 common IPC timeouts;
- #89 bounded mutex priority inversion;
- #90 stable-phase periodic timing;
- #37 reproducible latency/qualification model;
- #49 zero-cost timing/trace infrastructure;
- #50 scheduler/context-switch timing;
- #51 interrupt-to-task response;
- #52 IPC handoff/synchronization timing;
- #53 critical-section and bounded-operation time;
- #54 tick/release-jitter timing;
- #66 owner-death semantics if adopted;
- #76 architecture-neutral stack contract;
- #69 public diagnostic namespace cleanup;
- #87 optional SVC/privilege-boundary investigation.

## Timing terminology

Documentation must distinguish:

- measured average;
- measured maximum under a recorded test configuration;
- analytically derived upper bound;
- hardware/configuration-specific worst-case bound.

A measured maximum is not automatically WCET.

## Timing decomposition

Qualification separates:

1. **Interrupt/wake path**: hardware event/ISR entry where measurable, ISR producer cost, wake-to-READY, reschedule request, READY-to-task continuation, composite ISR-to-task latency.
2. **Scheduler/context switch**: scheduler decision, context save/restore, PendSV software interval, PendSV-to-task continuation.
3. **Synchronization**: primitive cost, waiter publication, timeout/priority-inversion bookkeeping, task continuation, ISR/task variants, queue-copy cost.
4. **Critical sections/bounded work**: interrupt-masked duration and cost as a function of configured task/waiter/payload limits.
5. **Timekeeping**: tick cost, sleep/deadline expiry, internal/external tick behavior, stable-phase periodic release and release jitter.

## Instrumentation rule

Timing hooks are compile-time selected and disabled in ordinary builds. Measurement images must keep instrumentation isolated from production semantics and account for probe overhead where it materially affects interpretation. On Cortex-M, DWT `CYCCNT` is the preferred cycle counter where supported.

## Design rule

When implementations provide equivalent behavior, HardRT prefers the design that is easier to bound and reason about over one that improves only average-case performance.

A feature that cannot be given deterministic resource and timing semantics must not be allowed to weaken a configuration advertised as hard real time merely for convenience.

## Release rule

Before 1.0.0, HardRT may publish configuration-specific measurements and state that it is engineered toward hard-real-time guarantees. It must not advertise an unconditional latency/WCET guarantee without the assumptions and evidence needed to support that claim.

The release sequence and implementation ownership are maintained in [ROADMAP.md](ROADMAP.md) and the 1.0 qualification umbrella #48.