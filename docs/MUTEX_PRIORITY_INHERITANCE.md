# HardRT 0.6 bounded mutex priority inheritance: implementation contract

Status: **design selected, implementation and physical qualification pending**. Tracked by [#89](https://github.com/ExoSpaceLabs/hardrt/issues/89) and 0.6 tracker #91. Baseline: `develop` commit `2790a604aae1bd5c37af5931f083573254674c27` (the merged #68 common IPC-timeout engine).

## Decision

Select **bounded, transitive priority inheritance (PI)** for priority-aware policies `HRT_SCHED_PRIORITY` and `HRT_SCHED_PRIORITY_RR`.

This preserves the existing `hrt_mutex_init`, `hrt_mutex_lock`, `hrt_mutex_lock_until`, FIFO waiter publication, non-recursive ownership and direct handoff. Immediate priority ceiling was considered, but would introduce application-supplied ceilings and/or system-ceiling lock-admission rules incompatible with the existing mutex contract. The selected PI protocol restores priorities when ownership/waiter dependencies disappear and propagates donation through bounded chains.

**Global `HRT_SCHED_RR` ignores priority by design.** A boost may be recorded for an eventual policy transition, but cannot produce fixed-priority inversion protection under global RR. Never advertise a PI-based blocking bound for that scheduler policy.

## Task and mutex metadata

Maintain distinct `base_prio` and `effective_prio` per task. Priority 0 is highest. The scheduler READY lookup, preemption and priority-RR grouping use effective priority for priority-aware policies, while base priority is immutable across inheritance changes.

Each application TCB tracks `waiting_mutex` (or no mutex) and a statically bounded set of **owned mutex references**. The mutex keeps existing owner/FIFO metadata. No dynamic allocation is allowed.

Introduce an explicit `HARDRT_MAX_OWNED_MUTEXES_PER_TASK` compile-time limit, default chosen and documented by the implementation, and reject a lock **before** acquiring ownership if its owner-reference slot cannot be reserved. Both uncontended and direct-handoff paths must honor the limit. The maximum total simultaneously owned mutex instances is bounded by `HARDRT_APP_MAX_TASKS * HARDRT_MAX_OWNED_MUTEXES_PER_TASK`. This is a capacity limit, **not** a silent count of arbitrary external mutex objects.

Mutex object lifetime is the caller's responsibility: it must remain valid while owned or waited upon. Destroy/reset of a live mutex is not safe. Owner-death recovery remains #66.

## Effective-priority rule

For task `T`, under priority-aware policies:

```text
effective(T) = min(base(T), effective(W) for each waiter W
                   on every mutex currently owned by T)
```

The fixed point includes nested and chained donation. FIFO order determines mutex handoff, **not** donation order; any waiter can cause donation. Expiring a timed high-priority waiter must withdraw its donation, potentially propagating a reduction along a chain.

Run propagation under the common kernel critical-section serialization from #68. Bound traversal explicitly by task count and owned-mutex capacity: no recursion, no unbounded linked walks, no heap allocation. Detect attempts to create a circular wait-for dependency *before publishing BLOCKED or timer membership* and fail deterministically without partial mutation. Cycles are not a supported runtime blocking mode.

Suggested safe implementation strategy: a fixed-capacity iterative worklist with a hard bound derived from `HARDRT_APP_MAX_TASKS` and `HARDRT_MAX_OWNED_MUTEXES_PER_TASK`; prove convergence and bound the number of updates. A full fixed-point recomputation with an explicit finite pass bound is acceptable in 0.6 if its cost is measured and documented; optimizing average-case performance is secondary.

## Scheduler membership and quantum

The existing READY structures have one intrusive link per task. Changing a READY task's effective priority must **atomically** remove it from its prior ready bucket and enqueue it in the new bucket, never temporarily let it appear twice or remain in the wrong bucket. The needed bounded arbitrary-READY-removal helper belongs in the scheduler core rather than the mutex module.

For a RUNNING owner, changing effective priority does not itself reset `slice_left`. If the change makes a higher-priority READY task eligible, request reschedule using the standard preemption mechanism. For a blocked task, propagate donation but do not mark it READY.

In `PRIORITY_RR`, retain the **remaining** quantum across temporary boosts, restorations and interruption by higher-priority work. Ordinary explicit yield and quantum exhaustion keep their documented semantics. `hrt_set_policy()` already rebuilds READY queues and intentionally resets quantums when changing scheduler policy; it must rebuild using effective priorities after PI metadata is consistent. Entering/leaving global RR must not leave stale priority bucket membership or destroy future donation dependencies.

## Transition contract

All transitions below occur under the kernel critical-section contract.

1. **Uncontended lock:** preflight owner-reference capacity, set owner, link into owner's bounded held set. Base/effective priority unchanged unless an existing dependency demands recalculation.
2. **Contended indefinite lock:** reject recursive/cyclic waits, publish FIFO waiter plus `waiting_mutex`, recompute effective priority through the owner chain, publish BLOCKED, reschedule.
3. **Contended timed lock:** same order, and atomically arm the #68 timer. On timeout, unlink FIFO + clear `waiting_mutex` + withdraw donation **before** publishing READY. On producer wake, disarm timeout before READY.
4. **Unlock with FIFO waiter:** remove mutex from old owner's held set; preflight/transfer into selected waiter's held set; clear new owner's `waiting_mutex`; perform inherited-priority recomputation for both owner graphs; hand off ownership directly; make waiter READY exactly once; preemption decision uses the final effective priorities.
5. **Unlock without waiter:** remove old owner reference, release ownership, restore/recompute owner effective priority, and reschedule if a ready task is newly higher-priority.
6. **Task exit:** retain existing ownership restrictions. Do not silently unlock, drop held-mutex tracking or recover application state. This is separate from #66.
7. **Policy change:** retain wait-for/held metadata and exact donor graph; rebuild target-policy READY queues under the existing policy transition rule.

The selected FIFO waiter may have inherited another priority; direct-handoff and cancellation must not assume queue order equals priority order.

## Boundedness and qualified scope

Let `N = HARDRT_APP_MAX_TASKS` and `M = HARDRT_MAX_OWNED_MUTEXES_PER_TASK`.

Static mutex metadata is O(N*M) references plus existing bounded per-mutex FIFO storage. Each owner reference and waiter scan is bounded by M and N respectively. Priority recomputation must ship with a **measured, explicit finite bound** as a function of N, M and priority classes. It must not rely on arbitrary application-created mutex counts.

A structural bound on the kernel protocol **alone is not a wall-clock blocking bound**. The application must independently bound time spent inside each owned critical section, hold a consistent lock acquisition order or rely on cycle rejection, and constrain higher-priority interference/IRQ behavior. Under those assumptions PI prevents unrelated medium-priority work from preempting a boosted low-priority owner; blocked chains are finite when the application graph is acyclic. Record the resulting configuration-specific response-time assumptions instead of calling observed maxima WCET.

Global RR has no priority-based mutex inversion guarantee. If the 0.6 qualification target needs a priority inversion bound, certify `PRIORITY` and/or `PRIORITY_RR`, not global RR.

## Tests required before feature merge

- Hosted low/medium/high classic inversion, donation and restoration.
- FIFO multi-waiter donation, handoff and final effective-priority restoration.
- Two+ held mutexes, nested mutexes, transitive donation and withdrawal.
- Timed high-priority waiter expiry/donation withdrawal at the exact #68 arbitration boundary.
- Cyclic waits rejected before state publication; waiter cancellation leaves no stale timer or owner reference.
- Owner reference limit and failure paths leave mutex/task state unchanged.
- READY reprioritization without duplicate intrusive membership.
- All three policies and runtime policy changes while mutexes are owned.
- Priority-RR remaining quantum preserved during boosts/restore; no unexpected reset.
- Task exit/owner-death exclusions remain enforced.
- Strict/UBSan, null/POSIX/Cortex-M, C/C++ API build and STM32H755 cross-build.
- New physical NUCLEO-H755ZI-Q functional fixture for classic inversion, nested/timeout handoff and priority restoration; timing-path measurements integrated with #52/#53.
- Full release candidate retains the unfiltered functional + 38 benchmark qualification gate, and must be rerun on the frozen 0.6 source SHA.

**Merge policy:** This design document may merge independently. The kernel implementation must remain a separate PR and must not merge until the hosted/cross-build gates and physical H755 functional gate pass.
