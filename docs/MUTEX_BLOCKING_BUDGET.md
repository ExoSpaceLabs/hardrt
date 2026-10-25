# Mutex blocking budget: conditional application analysis (0.6)

This describes how to **calculate a conservative finite mutex blocking budget
for one application configuration**, not a library-wide guarantee. HardRT's
bounded transitive priority inheritance (PI) provides a **finite loop count**
for donation bookkeeping; it cannot bound how long arbitrary user code runs
while holding a mutex. The 0.6 DWT uncontended diagnostics are observations,
not worst-case response-time, IRQ-interference or mutex-wait measurements.

## Applicability and proof obligations

Only use this model with `HRT_SCHED_PRIORITY` or
`HRT_SCHED_PRIORITY_RR`. Global `HRT_SCHED_RR` ignores priorities and has
**no PI-based priority-inversion guarantee**.

Record the actual compiled `HARDRT_APP_MAX_TASKS = N` (application tasks,
excluding the private idle task) and mutex-ownership cap
`HARDRT_MAX_MUTEXES_HELD = M`. The ring/owner graph has at most `N` task
nodes, each task waits on at most one mutex, nested cycles are rejected, and
each mutex has a FIFO of at most `N` task IDs. An acquiring task has at most
`N-1` *other* tasks queued/owning the mutex ahead of it. Subsequent arrivals
join **behind** it, not ahead.

To obtain a meaningful time bound, independently establish and retain:

1. `K`, maximum actual predecessor owners ahead at request time, including
   the current owner. Without stronger workload proof use `K=N-1`.
2. `H` (microseconds), a **finite upper bound on elapsed time** from an
   owner acquiring a resource until it releases it. It must also upper-bound
   the **remaining** tenure of an existing owner at the instant a waiter
   arrives. Include non-RTOS computation, nested/chained mutex waits,
   higher/equal-priority task interference, higher-priority ISR service,
   kernel work, any synchronous I/O or other waits. If a task can keep a
   mutex forever, `H` is **unbounded** and this calculation is invalid.
3. `D` (microseconds), a finite bound from each predecessor's unlock to the
   successor's **actual dispatch/resumption**, including the final requesting
   task. Include all interrupt and scheduler interference, not just PendSV
   execution. If same/higher-priority workloads can starve the successor,
   `D` is **unbounded** and this calculation is invalid.
4. A validated workload/locking graph, clock and cache/flash/SRAM placement,
   compiler flags, task priorities/quantum, interrupt ceiling and arrival
   model. Bound any mutual recursion in nested-lock tenure arguments rather
   than defining each `H` circularly from an as-yet-unbounded mutex wait.
5. No task may exit, reset or disable scheduling while retaining the mutex;
   there is no automatic owner-death recovery. This analysis must be redone
   after policy, clock, task-set or interference changes.

These are **application-level externally established elapsed-time maxima**.
They are not provided or proven by HardRT. Bounded kernel data structures do
not establish finite `H` or `D`. In strict priority mode an always-ready
same/higher-priority task may delay the waiter; PI only stops unrelated
*lower-than-donated*-priority tasks from causing uncontrolled inversion.

## Conservative bound

Each predecessor can consume at most `H` elapsed microseconds holding the
mutex and each owner-to-successor transition at most `D`. Because FIFO means
no arrival after the requester can overtake it, the requester's elapsed
blocking-to-resume interval has the conditional bound

```text
0 <= K <= N - 1
B_mutex_us <= K * (H_us + D_us)
```

This permits a deliberate overestimate of competing owners, chained waits
and scheduler costs; a more precise per-owner calculation may sum individual
`H_i + D_i` bounds if supported by actual application evidence. `M`
limits ownership/propagation work but does **not** by itself bound mutex tenure
or the execution of critical sections. For `K=0` the contended blocking bound
is zero; the uncontended kernel call still costs CPU time, which must be
accounted separately in task response-time analysis.

For **illustration only**, take a hypothetical configuration with `N=8`,
`K=7`, an independently *assumed* `H=200 us` and `D=40 us`:

```text
B_mutex_us <= 7*(200+40) = 1680 us
```

**Neither 200 us nor 40 us has been measured or qualified for HardRT.**
That illustration is not a supportable bound for the H755 or any deployed task
set. Values from the 0.6 DWT benchmark cannot be substituted for `H` or
`D`.

Use the fail-closed calculator with your **independently justified** inputs:

```bash
python3 scripts/mutex_blocking_bound.py \
    --policy HRT_SCHED_PRIORITY_RR \
    --task-capacity 8 --fifo-predecessors 7 \
    --hold-us 200 --handoff-us 40 --json
```

The output is always marked `CONDITIONAL_NOT_HARDWARE_QUALIFIED`. It rejects
global RR, a `K` exceeding the configured number of application slots and
unknown/zero/non-finite input bounds. The operator must retain evidence for
the input bounds alongside the source SHA and release configuration.

## Acceptance boundary

**0.6 establishes a sound parametrized accounting contract** and a way to
calculate finite application-specific blocking *only when the independent
assumptions are satisfied*. It is **not** a numerical qualification of any
specific STM32H755 application. #53/#52/#54 and the 0.7 timing campaign are
still required for interference-sensitive numerical analysis and evidence.
**Release-scope decision (2026-10-25, #91):** HardRT 0.6 acceptance is the
bounded PI bookkeeping/functional behavior, conditional parameterized mutex
blocking model, explicitly delimited 14-metric uncontended diagnostic
characterization, and the complete 16/52 physical campaign on the frozen
release candidate. No numeric H755 mutex wait-time or system WCET bound is
asserted. The original numerical blocking/interference proof requires a
concrete task/interrupt configuration with independently established `H`
and `D` plus supporting hardware evidence and is deferred to
#53/#52/#54 in **0.7**. This is a documented narrower milestone, **not a
claim that the numerical qualification has been completed**.
