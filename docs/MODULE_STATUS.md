# Module Status

HardRT 0.5.0 public/runtime components:

- [`inc/hardrt.h`](../inc/hardrt.h): lifecycle, tasks, scheduling, time queries, version/port identity, and diagnostics.
- [`inc/hardrt_time.h`](../inc/hardrt_time.h): application-owned external tick entry point.
- [`inc/hardrt_sem.h`](../inc/hardrt_sem.h): binary/counting semaphores and ISR give.
- [`inc/hardrt_mutex.h`](../inc/hardrt_mutex.h): owner-tracked non-recursive mutexes.
- [`inc/hardrt_queue.h`](../inc/hardrt_queue.h): fixed-capacity copy-based queues with task/ISR operations.
- [`inc/hardrt_event.h`](../inc/hardrt_event.h): statically allocated 32-bit event flags.
- [`inc/hardrt_notify.h`](../inc/hardrt_notify.h): per-task 32-bit notifications.
- [`src/internal/hardrt_port_contract.h`](../src/internal/hardrt_port_contract.h): authoritative non-installed port/core contract.
- [`cpp/hardrtpp.hpp`](../cpp/hardrtpp.hpp): optional header-only C++17 task/IPC wrappers.
- [`cpp/hardrt_signals.hpp`](../cpp/hardrt_signals.hpp): C++17 event/notification wrappers.
- generated `hardrt_version.h`: project version metadata.
- generated `hardrt_port.h`: selected port identity.

## Runtime contract

- Public lifecycle is `UNINITIALIZED -> INITIALIZED -> RUNNING` with explicit `hrt_status_t` failures for invalid initialization/start.
- TCB slot ownership (`UNUSED`/`USED`) is separate from execution state (`READY`, `RUNNING`, `SLEEP`, `BLOCKED`, `EXITED`).
- RUNNING tasks are outside READY storage; a READY task has exactly one READY representation.
- EXITED tasks do not execute and retain their used slot until safe reclamation by later task creation.
- Runtime task creation is supported. Live task-stack overlap is rejected.
- `HRT_SCHED_PRIORITY` is strict fixed-priority FIFO.
- `HRT_SCHED_RR` is one global FIFO independent of task priority.
- `HRT_SCHED_PRIORITY_RR` is fixed priority with RR rotation inside a priority class and retained unused quantum across higher-priority preemption.
- `hrt_sleep(0)` is an immediate scheduling point; positive durations use ceiling conversion to ticks.
- READY and sleeper storage are intrusive/static. No runtime heap allocation is used by the core.
- Port-owned ticks are configured during initialization and activated only at scheduler start.
- `hrt_tick_from_isr()` is valid only for `HRT_TICK_EXTERNAL`; the application must start its external tick only after scheduler execution begins.
- POSIX is a functional/scheduler simulator using Linux/glibc `ucontext` and signals, not a timing model.
- Cortex-M preserves required hard-float task context and uses PendSV for context switching.
- Cortex-M critical sections preserve stricter pre-existing BASEPRI masks and restore exact outer-entry state.

## Synchronization

- Semaphores: binary/counting, FIFO waiters, scheduler-aware task/ISR wake behavior.
- Mutexes: non-recursive, owner-tracked, FIFO/direct-handoff, task-context-only. v0.5 has no priority inheritance or automatic owner-death recovery.
- Queues: fixed-capacity caller storage, copy-based FIFO, task and non-blocking ISR operations.
- Event flags: 32-bit shared event object, wait-any/wait-all, retained or clear-on-exit bits, FIFO waiter publication, task/ISR producers.
- Task notifications: one private 32-bit value plus pending/wait state per application task, set-bits/overwrite/no-overwrite/saturating-increment actions, wait/take, task/ISR producers.

Generic IPC timeout variants remain outside v0.5.

## Validation status

The first event/notification development hardware pass at SHA `6f4ef62a8a0d13a0632537c6e65a50cbd315d656` completed:

```text
Board probe:           PASS
Functional contracts:  13 / 13 PASS
Historical benchmarks: 22 / 22 PASS
Overall:                PASS
```

The current v0.5 runner has since consolidated the 16 signal timing images into the same physical entry point, producing a final matrix of **13 functional contracts + 38 benchmark images**.

The release candidate must therefore be qualified with exactly:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

on the final frozen SHA. See [STM32_MANUAL_TESTS.md](STM32_MANUAL_TESTS.md).

Broader 1.0 hard-real-time work such as bounded mutex priority inversion, analytical critical-section/WCET bounds, queue-copy scaling, richer interference analysis, and machine-readable timing evidence remains separate.
