# Bounded mutex priority inheritance: 0.6 design

## Protocol decision

Select **bounded transitive priority inheritance (PI)** for
`HRT_SCHED_PRIORITY` and `HRT_SCHED_PRIORITY_RR`, retaining FIFO waiter
handoff. Immediate priority-ceiling protocols can achieve tighter blocking
bounds but require application-declared resource ceilings and global
system-ceiling admission, a breaking change to transparent mutex acquisition.
PI fits the existing API if held resources and wait chains have explicit
static bounds.

`HRT_SCHED_RR` intentionally ignores priorities. PI metadata remains
consistent if this policy is chosen, but **priority inversion cannot be
bounded by PI in global RR**. Applications requiring the fixed-priority
guarantee must select one of the two priority-aware schedulers.

## Static storage and finite algorithms

- `HARDRT_MAX_MUTEXES_HELD` defaults to 8; override at compile time.
  Each application TCB stores at most that many pointers to owned mutexes,
  a base priority, an effective priority and one waiting-on-mutex pointer.
  Attempts to acquire or wait for an additional mutex fail **before
  ownership or waiter state changes**.
- Every mutex stores no more than `HARDRT_APP_MAX_TASKS` FIFO waiter IDs.
  No global registry, dynamic allocation, timer task, or resource scan.
- Effective priority is the most urgent (numerically smallest) of task base
  priority and all effective priorities of waiters on its owned mutexes.
- On enqueue/removal/handoff, effective priority is recalculated and
  propagated through the blocked-owner chain. Wait-for cycles are detected
  and rejected before a task blocks.
- **Concrete bounded propagation algorithm:** after each wait-graph mutation,
  recompute the affected owner's effective priority by scanning its at-most
  M held mutex references and at-most N FIFO waiters per mutex. If that
  effective priority changes and this owner is itself blocked waiting on a
  mutex, repeat at the next owner in that task's single waiting-on edge.
  Stop when the value is unchanged, the chain ends, or N owners have been
  visited. This is a correct incremental update because the only dependency
  edges that can change value after a single enqueue/unlink/handoff are those
  along that owner chain. On handoff, recompute **both** old-owner and new-owner
  chains. Wait-for cycles are rejected before publishing a blocked edge.
- At most N distinct application owners can be visited; each recalculation
  visits at most M held mutexes times N waiters. Upper structural bound is
  **N*M*N = O(M*N²)** bounded waiter inspections per affected owner chain,
  plus O(N) cycle checking and READY requeue. A handoff recomputing two chains
  therefore has no more than **2*N*M*N** inspections plus bounded queue
  operations. The implementation uses no recursion and no periodic TCB scan.
  These are structural loop counts, **not** cycle/WCET guarantees.

## Scheduler and lifetime invariants

- A READY task whose effective priority changes is removed from its **old**
  priority FIFO before its priority field changes, then inserted in the
  correct FIFO. One READY membership only, no quantum reset on priority
  donation/withdrawal. FIFO order of unaffected same-priority tasks stays.
- RUNNING/BLOCKED tasks change priority without queue membership changes.
  For priority-aware policies, demoting a RUNNING owner after unlock may
  require rescheduling if higher-priority READY tasks exist.
- Policy transitions retain PI graph and priorities; the already documented
  policy-change operation rebuilds READY structures and resets quantum.
  Transient inheritance itself does not reset PRIORITY_RR remaining quantum.
- Successful handoff first detaches ownership from old owner, attaches to
  FIFO winner, clears winner's waiting edge, recomputes priorities, disarms
  any timed wait, and publishes the winner READY exactly once.
- Timed expiry unlinks the mutex FIFO waiter, clears its wait edge and
  withdraws donation **before** READY publication. Producer wake and
  timeout use one critical-section serialization. Neither may win twice.
- Recursive lock and wait-for deadlock cycles are rejected. Tasks must
  release held locks before exiting; robust owner-death recovery belongs
  to #66. Reinitializing a locked mutex is invalid caller behavior.

## Actual blocking guarantees

PI prevents unrelated medium-priority tasks from preempting a boosted
lower-priority owner under fixed-priority scheduling. It does **not** alone
prove a numerical worst-case blocking duration. The application must bound
the execution of each critical section, nesting depth, IRQ interference,
and any non-mutex blocking while owning a mutex. FIFO handoff can introduce
multiple blocking terms. The reproducible **conditional configuration-specific
blocking calculation** is in [MUTEX_BLOCKING_BUDGET.md](MUTEX_BLOCKING_BUDGET.md):
for up to K preceding owners, each with a separately justified finite elapsed
tenure H and unlock-to-dispatch bound D, `B_mutex <= K*(H+D)`, `K<=N-1`.
Without finite evidence for H and D, the time bound is unavailable.
Characterizing the kernel's critical-section and handoff costs remains part
of #52/#53; timing measurements are not WCET proofs.

## Test/qualification gate

Validate low/medium/high inversion, nested/transitive ownership, FIFO
waiters, timed donation withdrawal, waiter timeout/unlock edge ordering,
cycle and held-capacity rejection, READY migration, all policies, retained
PRIORITY_RR quantum, and runtime policy switching. Run strict/UBSan and
Cortex-M cross-builds. Require physical H755 functional validation before
merging. Full 0.6 release requires unfiltered physical benchmark evidence.

## Relation to the original design review

The original pre-implementation review considered a full `N-1`-pass fixed-point recomputation with an O(N³*M) bound. The implementation uses the tighter bounded **incremental chain propagation** specified above instead. The safe substitution depends on two enforced invariants: each blocked task waits on no more than one mutex, and cycles are rejected before wait-graph mutation. The hosting tests must exercise nested/transitive donation, timer-driven withdrawal, FIFO handoff, and runtime policy switching. The original full recomputation remains a fallback design if those invariants cannot be established. Timing evidence under #52/#53 is still required before making configuration-specific WCET claims.
