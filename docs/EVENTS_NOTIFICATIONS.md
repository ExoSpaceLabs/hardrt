# Event flags and task notifications

HardRT v0.5.0 adds two complementary signaling mechanisms:

- **event flags**: an explicit statically allocated object for many-to-many bitmask synchronization;
- **task notifications**: one private 32-bit notification word per application task for lightweight one-to-one signaling.

Both mechanisms use the existing HardRT scheduler wake contract. They allocate no memory, create no worker task, and keep ISR producers non-blocking.

## Shared conventions

- Public payload width is `uint32_t`.
- Blocking waits are task-context only.
- ISR APIs never block and report the consolidated scheduling decision through `need_switch`, using the same meaning as semaphore and queue ISR APIs.
- Application task IDs are the IDs returned by `hrt_create_task()`.
- The private idle task is never a valid notification target and never joins an event wait list.
- IPC-style functions return `0` on success and `-1` on invalid arguments/state or a rejected operation.
- Timeout variants are outside this feature slice. Event and notification waits block indefinitely until satisfied.

## Event flags

### State

An `hrt_event_t` owns one 32-bit flag word and bounded waiter metadata sized by `HARDRT_APP_MAX_TASKS`. No backing allocation is required beyond the event object itself.

### Wait conditions

`hrt_event_wait()` requires a non-zero mask.

- **wait-any** is the default: the wait is satisfied when `(bits & mask) != 0`.
- **wait-all** uses `HRT_EVENT_WAIT_ALL`: the wait is satisfied when `(bits & mask) == mask`.
- The returned `matched` value is the matching subset from the event snapshot that satisfied the waiter.
- `HRT_EVENT_CLEAR_ON_EXIT` clears the matched bits after satisfaction. Without it, bits are retained.

When one update satisfies multiple waiters, all waiters are evaluated against the same post-set snapshot. Therefore one waiter's clear-on-exit policy cannot prevent another waiter that was satisfied by that same update from waking. After the scan, the union of matched bits requested for clear-on-exit is cleared once.

### Waiter order

Blocked waiters are stored in registration FIFO order. A set operation inspects at most `HARDRT_APP_MAX_TASKS` waiter records and publishes every satisfied waiter READY in that FIFO order. The scheduler then applies the active policy normally:

- strict priority selects the highest-priority READY task;
- global RR preserves FIFO publication order;
- priority RR preserves FIFO order within each priority class.

A task may appear at most once in one event wait list.

### Operations

- `hrt_event_init()` initializes the object with zero bits and no waiters.
- `hrt_event_get()` returns a critical-section-consistent snapshot.
- `hrt_event_set()` ORs bits into the event and wakes every newly satisfied waiter.
- `hrt_event_set_from_isr()` performs the same state transition without directly switching context.
- `hrt_event_clear()` clears selected bits and never wakes waiters.
- `hrt_event_clear_from_isr()` is a bounded non-blocking clear operation and never requests a switch.
- Setting or clearing a zero bit mask is a valid no-op.

## Task notifications

### Per-task state

Each application TCB owns:

- one 32-bit notification value;
- one pending flag;
- one flag indicating that the task is blocked specifically on its notification.

This state is reset when a TCB slot is first created or reused. It is private kernel state and does not alter the public task object model.

A notification sent while the target is READY or RUNNING updates the value and pending state but does not change scheduling membership. A notification wakes a task only when that task is blocked specifically in `hrt_task_notify_wait()` or `hrt_task_notify_take()`; it does not wake a task blocked on a semaphore, queue, mutex, event, or sleep.

### Producer actions

`hrt_task_notify()` and `hrt_task_notify_from_isr()` support:

- `HRT_NOTIFY_SET_BITS`: `notification |= value`;
- `HRT_NOTIFY_OVERWRITE`: replace the current value even when already pending;
- `HRT_NOTIFY_NO_OVERWRITE`: replace the value only when no notification is pending, otherwise fail with `-1` and preserve the prior notification;
- `HRT_NOTIFY_INCREMENT`: increment by one, saturating at `UINT32_MAX`; the `value` argument is ignored.

Every successful producer action marks the notification pending.

Valid targets are live application tasks with `HRT_SLOT_USED` and execution state other than `HRT_EXITED`. Invalid IDs, idle, unused slots, and exited tasks are rejected.

### Wait

`hrt_task_notify_wait(clear_on_entry, clear_on_exit, value)` operates on the calling task.

1. `clear_on_entry` bits are cleared before checking the pending flag.
2. If a notification is already pending, the call returns immediately.
3. Otherwise the task blocks specifically on its notification.
4. On satisfaction, `*value` receives the notification value before exit clearing.
5. `clear_on_exit` bits are then cleared from the stored value.
6. The pending flag is consumed by the successful wait.

Notifications sent before the task waits are therefore preserved.

### Take

`hrt_task_notify_take(clear_count_on_exit)` is the counting-notification convenience operation. It blocks until the notification value is non-zero, returns the pre-consumption value, and consumes the pending state. If `clear_count_on_exit` is non-zero, the stored value becomes zero; otherwise it is decremented by one. Increment saturation prevents count rollover from silently becoming zero.

### ISR behavior

ISR producers use the existing kernel critical-section contract. `need_switch` is set to one when the notification or event update makes a task eligible that should run before the interrupted/current task under the active scheduler policy, or when no normal application task is running. HardRT pends the context switch through the port and never performs a direct ISR context switch.

## Determinism and storage

No operation allocates, recurses, or uses a hidden worker. Event-set cost is bounded by configured application task capacity. Task-notification producer cost is O(1).

For the default `HARDRT_APP_MAX_TASKS=8` configuration:

- `sizeof(hrt_event_t) == 96` bytes;
- the Cortex-M private TCB grows from 32 bytes to 40 bytes because the notification value/state adds six source bytes and natural 32-bit alignment rounds the structure growth to eight bytes per application task;
- the signaling primitives add no dedicated task stack and no dynamic allocation;
- call-frame stack use is compiler/optimization dependent, but the implementation is bounded and non-recursive.

The hosted test suite prints the configured event-object and private-TCB sizes and explicitly verifies the default event-object size. A test-only registration hook fills every configured event waiter slot to prove bounded waiter storage at exact capacity without creating a fake production API.

Exact `_hrt_tcb_t` layout remains private and is not an ABI guarantee.

## Deterministic stress and invariants

The v0.5 hosted suite includes a bounded deterministic signal-stress group. It uses seed `0x00C0FFEE` and executes 1,024 mixed event/notification operations under each scheduler policy:

- `HRT_SCHED_PRIORITY`;
- `HRT_SCHED_RR`;
- `HRT_SCHED_PRIORITY_RR`.

The sequence mixes wait-any/wait-all, retained and clear-on-exit event bits, all notification producer actions, ISR-facing producer APIs, and external tick activity. Invariant checks verify after every relevant transition that:

- READY tasks appear exactly once in the ready queues;
- RUNNING/BLOCKED/SLEEP/EXITED tasks do not appear in a ready queue;
- event waiter queue membership and `wait_active[]` agree with each other;
- every registered event waiter is actually BLOCKED;
- a task marked as waiting on a notification is BLOCKED;
- unused slots have no ready or event-waiter membership.

Dedicated deterministic cases additionally cover notification clear-on-entry/clear-on-exit, a 64-increment burst, saturation, READY/RUNNING/EXITED target states, simultaneous event + notification wakeups, and same-priority publication order.

The tests run in normal POSIX CI and in a strict-warning + UBSan job. Task return/deletion **while blocked** is not an applicable public-API state in v0.5: HardRT only exposes deletion of the current task, and a blocked task cannot execute its own return/delete path. External deletion of another blocked task would require a future lifecycle API and its own waiter-unlink contract.

## STM32 timing qualification

Functional correctness alone is not sufficient evidence for the ISR-facing event path because `hrt_event_set_from_isr()` performs a bounded waiter scan while interrupts are masked by the HardRT critical-section contract. v0.5.0 therefore profiles the new primitives on the STM32H755 CM7 using the same DWT cycle counter used by the existing scheduler and tick qualification.

The signal timing images use direct application-side DWT timestamps with `HARDRT_TIMING_PROFILE=none`. The production event and notification implementations are not rebuilt with timing hooks.

Required measurements are:

- `event_isr_to_task`: `hrt_event_set_from_isr()` entry to a higher-priority event waiter continuing after `hrt_event_wait()`;
- `notify_isr_to_task`: `hrt_task_notify_from_isr()` entry to a higher-priority notification waiter continuing after `hrt_task_notify_wait()`;
- `event_scan_none`: event-set ISR call cost with no matching waiter;
- `event_scan_one`: event-set ISR call cost with exactly one matching waiter;
- `event_scan_all`: event-set ISR call cost with all registered waiters matching;
- `notify_isr_no_wake`: notification ISR call cost when no task becomes READY;
- `notify_isr_wake`: notification ISR call cost when the blocked target becomes READY.

The three event-scan cases are executed with **1, 8, 16 and 32 actual registered waiters**. The firmware verifies expected wake counts on every sample series, so a low cycle count cannot pass merely because the requested waiters failed to participate.

All physical validation and profiling is executed by the single qualification runner:

```bash
scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7
```

A full run executes the 13 functional contracts plus all 38 benchmark images, including the 16 signal-profile images, at 10,000 samples per benchmark image by default. The legacy `event_to_task` timing case remains semaphore-backed for historical comparability and must not be interpreted as an event-flags measurement.

The measurements are engineering timing characterizations, not formal WCET proofs. For release claims, the maximum observed event-set critical-section cost must be reported together with the waiter count and wake fan-out that produced it.

## Timeout scope

Generic IPC timeout work is intentionally separate. Once the common timeout contract lands, event and notification timeout variants can use the same wrap-safe tick/deadline machinery rather than embedding a second timeout model here.
