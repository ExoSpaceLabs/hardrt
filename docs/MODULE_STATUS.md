# Module Status

HardRT **0.6.0** public/runtime components (root `VERSION` is the build source of truth):

- [`inc/hardrt.h`](../inc/hardrt.h): lifecycle, tasks, scheduling, absolute periodic release, common finite-wait result and version/port identity.
- [`inc/hardrt_time.h`](../inc/hardrt_time.h): application-owned external tick.
- [`inc/hardrt_sem.h`](../inc/hardrt_sem.h): counting/binary semaphores, FIFO waiters, ISR give, timed take.
- [`inc/hardrt_mutex.h`](../inc/hardrt_mutex.h): non-recursive owner-tracked mutex, timed lock, bounded transitive priority inheritance.
- [`inc/hardrt_queue.h`](../inc/hardrt_queue.h): caller-storage, copy-based bounded FIFO with timed send/receive and ISR operations.
- [`inc/hardrt_event.h`](../inc/hardrt_event.h): 32-bit event flags and absolute deadline wait.
- [`inc/hardrt_notify.h`](../inc/hardrt_notify.h): per-task notifications and absolute deadline wait/take.
- [`src/internal/hardrt_port_contract.h`](../src/internal/hardrt_port_contract.h): non-installed port/core contract.
- [`cpp/hardrtpp.hpp`](../cpp/hardrtpp.hpp) and [`cpp/hardrt_signals.hpp`](../cpp/hardrt_signals.hpp): optional header-only C++17 API.
- generated `hardrt_version.h` / `hardrt_port.h`: compile-time version and port metadata.

## Scheduler and memory

- `UNINITIALIZED -> INITIALIZED -> RUNNING`; separate slot `UNUSED/USED` and task `READY/RUNNING/SLEEP/BLOCKED/EXITED` state.
- `HRT_SCHED_PRIORITY`: strict fixed-priority FIFO; `HRT_SCHED_RR`: global priority-independent FIFO; `HRT_SCHED_PRIORITY_RR`: fixed-priority RR, preserved quantum across higher-priority preemption.
- Application tasks use bounded intrusive READY, sleep/deadline and IPC waiter storage; no runtime heap allocation in the kernel core. Tasks may be created at runtime and safely reclaim EXITED slots.
- `hrt_sleep(0)` yields; `hrt_delay_until()` observes late releases without resetting nominal phase. The common finite-wait APIs use absolute wrapping tick deadlines with a half-range ambiguity rule and deterministic wake/expiry serialization.
- Cortex-M uses PendSV for context switching, preserves FPU state and stronger existing BASEPRI masks; SVC/MPU is future work (#87).
- POSIX uses pthread-backed hosted execution with reserved `SIGALRM` / `SIGUSR2` and is a *functional*, not hard-real-time timing, target.

## Synchronization

- Semaphores and queues: blocking, immediate try and absolute-deadline operations.
- Mutexes: task-context-only, non-recursive, FIFO/direct handoff and **bounded transitive PI** with statically limited owned-mutex/chain capacity under priority-aware policies. Global RR ignores priority; no owner-death recovery.
- Events / notifications: static 32-bit state with task/ISR producers and finite-wait alternatives.
- Queue `memcpy()` runs inside the critical region, so worst-observed critical-section cost depends on application-selected item size. Full interference/WCET qualification remains open.

## Validation

The full release-grade STM32H755 entry point remains:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

0.6 qualification requires **16/16 functional + 52/52 benchmark metrics across 39 build/flash images**, clean tracked source, probe PASS and raw evidence. The final complete unfiltered NUCLEO-H755ZI-Q CM7 run on `bf903a50b984572a04e1c201b79ecac4f0d58b6e` passed 16/16 functional and 52/52 benchmark metrics, with probe PASS and overall PASS. Promotion PR #110 merged this qualified source into `main`. Publication remains separately gated by tag, CI, software packages and operator-attached physical evidence. Qualified-source SHA and the `main` merge/tag SHA may differ only if the conservative qualified-build-input equivalence guard passes. Hardware evidence stays in locally retained archives and is attached to a draft GitHub release by the operator, not synthesized by CI.

Historical published `0.5.0`/`0.5.1` contracts (13 functional + 38 benchmark cases) remain documented in [QUALIFICATION.md](QUALIFICATION.md) and [QUALIFICATION_0_5_1.md](QUALIFICATION_0_5_1.md).

Formal cross-lane interference, numerical blocking/WCET bounds and full supported-configuration hard-real-time qualification remain tracked under #48/#52/#53/#54.
