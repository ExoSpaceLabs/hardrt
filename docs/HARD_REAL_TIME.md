# Hard real-time qualification

HardRT is intended to evolve toward documented hard real-time behavior on supported Cortex-M configurations.

This document defines the engineering direction and the current qualification boundary. It is not a claim that every HardRT configuration is already hard real time.

## Qualification boundary

Hard real-time qualification applies only to explicitly supported Cortex-M targets and configurations for which the required timing and interference assumptions are recorded.

The POSIX port is excluded from hard real-time qualification. It is a functional and scheduler-validation environment and does not model Cortex-M execution timing.

## Current v0.5 development status

The v0.5 scheduler/lifecycle hardening phase has an accepted physical-hardware baseline on NUCLEO-H755ZI-Q CM7:

```text
Run ID:       20260905T134123Z_80f2042f
HardRT SHA:   80f2042f2c64053a9ea888666474c5dad5f72797
Tracked tree: clean
STM32CubeH7:  f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
Functional:   11 / 11 PASS
Benchmarks:   22 / 22 PASS
Overall:      PASS
```

The accepted suite covers task/context progress, scheduler-policy behavior, lifecycle-sensitive task creation/reuse, fixed-priority and RR wake behavior, semaphores, queues, mutexes, external tick integration, `BASEPRI` preservation, scheduler/PendSV timing and tick/sleeper scaling through 32 configured application tasks.

This evidence is sufficient to close the **v0.5 scheduler/lifecycle hardening phase**. It is not a universal WCET proof and it is not yet the final v0.5.0 release package. The final RC must repeat the full unfiltered hardware qualification on the exact frozen release SHA.

## Required properties for a formal hard-real-time configuration

A HardRT configuration may be described as hard real time only when all relevant kernel operations and interference sources have finite, documented bounds under a stated configuration.

At minimum, qualification requires:

1. **Static resource bounds**
   - no dynamic allocation in kernel runtime paths;
   - fixed maximum task, waiter, queue, event and notification capacities;
   - documented stack and static-memory requirements.

2. **Bounded scheduler behavior**
   - deterministic ready-task selection;
   - bounded ready-queue insertion/removal/selection;
   - explicit behavior for all scheduler policies;
   - no duplicate or stale task membership in scheduler structures.

3. **Bounded interrupt behavior**
   - defined interrupt-priority ceiling for kernel-aware ISRs;
   - bounded critical sections;
   - no blocking operations from ISR context;
   - explicit scheduling decision after ISR wake-up.

4. **Bounded synchronization behavior**
   - bounded waiter inspection and handoff;
   - explicit priority-inversion strategy for mutex ownership;
   - deterministic wake ordering;
   - bounded timeout processing where timeout APIs exist.

5. **Lifecycle and error determinism**
   - invalid configuration and invalid lifecycle transitions fail predictably;
   - kernel corruption or contract violations are observable;
   - debug/release differences do not invalidate the behavioral contract.

6. **Reproducible timing evidence**
   - exact HardRT commit;
   - compiler, binutils, optimization and link flags;
   - MCU/core, board revision, clock tree and flash/SRAM placement;
   - cache and FPU state;
   - tick source and frequency;
   - IRQ priorities and syscall interrupt ceiling;
   - task priorities and scheduler policy;
   - raw machine-readable timing results.

## What the v0.5 hardening already establishes

The current development baseline provides strong evidence for these parts of the model:

- static kernel/task storage and no runtime heap allocation;
- deterministic policy-specific READY storage;
- O(1) no-expiry tick work with the intrusive delta sleeper queue;
- O(K) wake processing for K sleepers expiring together;
- scheduler-aware wake/preemption behavior;
- separation of RUNNING from READY membership;
- explicit slot ownership versus task execution state;
- Cortex-M hard-float context preservation;
- preserved stricter caller `BASEPRI` state and nested critical-section restoration;
- public lifecycle/configuration failure semantics;
- external-tick source ownership and startup ordering;
- physical H755 coverage for semaphore, queue and mutex wake/handoff paths.

## Remaining hard-real-time work

The following work remains intentionally open and is **post-v0.5/non-blocking for the current scheduler-hardening phase**:

- maximum observed and analytical bounds for all kernel critical sections;
- queue `memcpy()` scaling and a bounded item-size/design decision;
- explicit priority-inversion mitigation for mutexes;
- robust/owner-death mutex semantics if adopted;
- complete interference matrices with higher-priority interrupts/tasks;
- true hardware-event-to-ISR-entry measurements where practical;
- complete machine-readable timing output;
- complete board/cache/FPU/memory/compile/link metadata in release evidence;
- periodic-task timing primitives and release-jitter characterization;
- event/notification timing once those APIs are implemented.

These items remain tracked by #37 and #48 with supporting work in #49–#54 and #66. Keeping them open is deliberate: passing the current development suite does not justify pretending these broader 1.0-quality questions disappeared.

## Timing terminology

HardRT documentation must distinguish:

- a measured average;
- a measured maximum under a recorded test configuration;
- an analytically derived upper bound;
- a hardware/configuration-specific worst-case bound.

A measured maximum is not, by itself, a worst-case execution-time proof.

## Timing decomposition

The qualification architecture separates:

1. **Interrupt/wake path**
   - kernel-aware ISR service duration;
   - task wake to READY transition;
   - reschedule request;
   - READY task to resumed task continuation;
   - complete event-to-task response as a composite metric.

2. **Scheduler/context switch**
   - scheduler decision;
   - outgoing context save;
   - incoming context restore;
   - complete PendSV software interval;
   - PendSV entry to resumed task continuation.

3. **Synchronization**
   - primitive execution cost;
   - waiter selection and READY latency;
   - waiter RUNNING latency;
   - ISR-aware operation separately from task-context operation;
   - queue copy cost separately from scheduler/handoff cost.

4. **Critical sections and bounded work**
   - interrupt-masked duration;
   - READY operations;
   - sleeper insertion/wake processing;
   - operation cost as a function of configured task, priority, waiter and item-size bounds.

5. **Timekeeping/release jitter**
   - tick processing cost;
   - sleep-expiry to READY/RUNNING latency;
   - internal and external tick paths separately;
   - periodic-task release jitter once an absolute periodic timing primitive exists.

## Instrumentation rule

Timing hooks are compile-time selected and disabled in ordinary builds. Instrumented images must quantify or at least acknowledge probe overhead, and measurement points must remain isolated enough that unrelated instrumentation does not silently contaminate a narrower metric.

On Cortex-M, DWT `CYCCNT` is the preferred cycle counter where supported. Scheduler/PendSV decomposition may require measurement-specific switch instrumentation, but the production scheduling semantics must remain unchanged.

## Design rule

When two implementations provide equivalent functionality, prefer the implementation that is easier to bound and reason about, even if another implementation saves a small amount of memory or improves average-case performance.

Optimizations that make scheduler, ISR or synchronization behavior harder to analyze require evidence that the trade remains compatible with the qualification model.

## Release rule

Before 1.0.0, HardRT may state that it is designed **toward hard real-time guarantees** and may publish configuration-specific measured results.

A release must not make an unconditional hard-real-time latency guarantee until its supported target/configuration assumptions and corresponding evidence are published.
