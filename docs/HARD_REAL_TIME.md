# Hard real-time qualification

HardRT is intended to evolve toward documented hard real-time behavior on supported Cortex-M configurations.

This document defines the engineering direction. It is not a claim that every current HardRT configuration is already hard real-time.

## Qualification boundary

Hard real-time qualification applies only to explicitly supported Cortex-M targets and configurations for which the required timing and interference assumptions are recorded.

The POSIX port is excluded from hard real-time qualification. It is a functional and scheduler-validation environment and does not model Cortex-M execution timing.

## Required properties

A HardRT configuration may be described as hard real-time only when all relevant kernel operations and interference sources have finite, documented bounds under a stated configuration.

At minimum, qualification requires:

1. **Static resource bounds**
   - no dynamic allocation in kernel runtime paths;
   - fixed maximum task, waiter, queue, event, and notification capacities;
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
   - release behavior does not silently change between debug and non-debug builds in ways that invalidate timing assumptions.

6. **Reproducible timing evidence**
   - exact HardRT commit;
   - compiler, binutils, optimization, and link flags;
   - MCU/core, board revision, clock tree, flash/SRAM placement;
   - cache and FPU state;
   - tick source and frequency;
   - IRQ priorities and syscall interrupt ceiling;
   - task priorities and scheduler policy;
   - raw machine-readable timing results.

## Timing terminology

HardRT documentation must distinguish:

- a measured average;
- a measured maximum under a recorded test configuration;
- an analytically derived upper bound;
- a hardware/configuration-specific worst-case bound.

A measured maximum is not, by itself, a worst-case execution-time proof.

## Design rule

When two implementations provide equivalent functionality, prefer the implementation that is easier to bound and reason about, even if another implementation saves a small amount of memory or improves average-case performance.

Optimizations that make scheduler, ISR, or synchronization behavior harder to analyze require evidence that the trade remains compatible with the qualification model.

## Release rule

Before 1.0.0, HardRT may state that it is designed **toward hard real-time guarantees**.

A release must not make an unconditional hard-real-time latency guarantee until its supported target/configuration assumptions and corresponding evidence are published.
