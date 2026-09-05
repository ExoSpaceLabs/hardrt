# Hard real-time qualification

HardRT is being engineered toward documented hard real-time behavior on explicitly supported Cortex-M configurations. This page defines that engineering boundary; it is not a claim that every build is already hard real time.

## Qualification boundary

Hard-real-time qualification applies only to Cortex-M targets/configurations whose timing and interference assumptions are recorded. The POSIX port is a functional/scheduler validation environment and is excluded from timing qualification.

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

These results are development measurements, not a universal WCET proof.

## Required properties for a formal hard-real-time configuration

A HardRT configuration can be described as hard real time only when relevant kernel operations and interference sources have finite, documented bounds under explicit assumptions.

### Static resource bounds

- no dynamic allocation in kernel runtime paths;
- fixed task, waiter, event, notification, and queue capacities;
- documented stack/static-memory costs.

### Bounded scheduler behavior

- deterministic READY selection and insertion/removal;
- explicit behavior for every scheduler policy;
- no duplicate/stale scheduler membership;
- bounded sleeper insertion and expiry processing.

### Bounded interrupt behavior

- documented interrupt-priority ceiling for kernel-aware ISRs;
- bounded critical sections;
- no ISR blocking;
- explicit scheduler decision after ISR wake.

### Bounded synchronization behavior

- deterministic waiter ordering/handoff;
- bounded event waiter inspection;
- O(1) notification producer work;
- explicit priority-inversion strategy for mutexes in any configuration that claims a formal bound;
- bounded timeout processing if/when timeout APIs exist.

### Lifecycle/error determinism

- invalid configuration/lifecycle transitions fail predictably;
- contract violations are observable;
- debug/release differences do not silently change public behavior.

### Reproducible timing evidence

Formal claims require, as applicable:

- exact HardRT commit;
- compiler/binutils versions and compile/link flags;
- MCU/core and board revision;
- clock tree and flash/SRAM placement;
- cache/FPU/lazy-stacking state;
- tick source/frequency;
- IRQ priorities and syscall ceiling;
- task priorities and scheduler policy;
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

## Remaining 1.0-quality work

These items remain deliberately open and do not block v0.5 while release documentation avoids unsupported universal guarantees:

- analytical/max critical-section bounds for all kernel paths;
- queue `memcpy()` scaling and an explicit payload-size design bound;
- bounded mutex priority-inversion mitigation;
- robust/owner-death mutex semantics if adopted;
- richer higher-priority interrupt/task interference matrices;
- true hardware-event-to-ISR-entry measurement where practical;
- complete machine-readable timing output;
- complete board/cache/FPU/memory/compile/link metadata;
- periodic-task/release-jitter primitives and qualification.

Tracked primarily by #37 and #48 with supporting #49–#54 and #66.

## Timing terminology

Documentation must distinguish:

- measured average;
- measured maximum under a recorded test configuration;
- analytically derived upper bound;
- hardware/configuration-specific worst-case bound.

A measured maximum is not automatically WCET.

## Timing decomposition

Qualification separates:

1. **Interrupt/wake path**: ISR producer cost, wake-to-READY, reschedule request, READY-to-task continuation, composite ISR-to-task latency.
2. **Scheduler/context switch**: scheduler decision, context save/restore, PendSV software interval, PendSV-to-task continuation.
3. **Synchronization**: primitive cost, waiter publication, task continuation, ISR/task variants, queue-copy cost.
4. **Critical sections/bounded work**: interrupt-masked duration and cost as a function of configured task/waiter/payload limits.
5. **Timekeeping**: tick cost, sleep expiry, internal/external tick behavior, and future periodic-release jitter.

## Instrumentation rule

Timing hooks are compile-time selected and disabled in ordinary builds. Measurement images must keep instrumentation isolated from production semantics and account for probe overhead where it materially affects interpretation. On Cortex-M, DWT `CYCCNT` is the preferred cycle counter where supported.

## Design rule

When implementations provide equivalent behavior, HardRT prefers the design that is easier to bound and reason about over one that improves only average-case performance.

## Release rule

Before 1.0.0, HardRT may publish configuration-specific measurements and state that it is engineered toward hard-real-time guarantees. It must not advertise an unconditional latency/WCET guarantee without the assumptions and evidence needed to support that claim.
