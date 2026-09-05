# Module Status

Current components on the HardRT `develop` branch:

- [`inc/hardrt.h`](../inc/hardrt.h): task, scheduler, configuration, time-query, version, port-identity, and diagnostic declarations.
- [`inc/hardrt_time.h`](../inc/hardrt_time.h): public application-owned external-tick entry point `hrt_tick_from_isr()`.
- [`inc/hardrt_sem.h`](../inc/hardrt_sem.h): binary/counting semaphores and ISR give.
- [`inc/hardrt_mutex.h`](../inc/hardrt_mutex.h): owner-tracked non-recursive mutexes.
- [`inc/hardrt_queue.h`](../inc/hardrt_queue.h): fixed-size copy-based queues with task and ISR operations.
- [`src/internal/hardrt_port_contract.h`](../src/internal/hardrt_port_contract.h): authoritative non-installed port/core contract.
- [`cpp/hardrtpp.hpp`](../cpp/hardrtpp.hpp): optional header-only C++17 wrappers.
- generated `hardrt_version.h`: project version metadata.
- generated `hardrt_port.h`: selected port identity.

## Current status notes

- Released version: `0.4.0`; `develop` contains the v0.5.0 work in progress.
- The public lifecycle is `UNINITIALIZED -> INITIALIZED -> RUNNING`; repeated initialization/start and invalid runtime configuration return explicit `hrt_status_t` failures.
- TCB slot ownership (`UNUSED`/`USED`) is separate from task execution state (`READY`, `RUNNING`, `SLEEP`, `BLOCKED`, `EXITED`).
- A RUNNING task is not represented in READY storage. EXITED tasks remain valid used slots until a later task creation reclaims them.
- Runtime task creation is supported while the scheduler is RUNNING. Stack overlap with any live task is rejected; exact-stack reuse of an EXITED task is supported through slot reclamation.
- Priority values `HRT_PRIO0` through `HRT_PRIO11` are declared; the build-time usable range is `0` through `HARDRT_MAX_PRIO - 1`.
- `HARDRT_CFG_MAX_TASKS` / `HARDRT_APP_MAX_TASKS` describe creatable application tasks. One additional private idle slot is reserved internally; `HARDRT_MAX_TASKS` remains the legacy total-slot alias.
- `HRT_SCHED_PRIORITY` uses strict fixed-priority FIFO scheduling.
- `HRT_SCHED_RR` on `develop` uses one global FIFO and ignores task priority.
- `HRT_SCHED_PRIORITY_RR` uses fixed-priority selection with RR rotation inside each priority class and retains interrupted-task queue precedence/unused quantum after higher-priority preemption.
- READY membership is tracked in intrusive ready-link storage and duplicate membership protection is enforced independently of debug builds.
- Sleeping tasks use one intrusive delta queue: insertion is bounded O(N) in task context, no-expiry tick work is O(1), and waking K expired tasks is O(K) plus READY insertion work.
- Port-owned periodic ticks call private `hrt__tick_isr()`. Their timer is configured during `hrt_init()` but activated only at scheduler start.
- The public `hrt_tick_from_isr()` API is for `HRT_TICK_EXTERNAL` only. The application must not start routing the external periodic interrupt into HardRT before scheduler execution has begun.
- Tick processing performs scheduler-aware wake decisions and requests rescheduling internally; application ISRs do not call a separate yield-from-ISR API.
- POSIX uses Linux/glibc `ucontext`, signal-driven tick accounting and cooperative context handoff at HardRT scheduling points. It is a functional simulator, not a Cortex-M timing model.
- Cortex-M hard-float task context is preserved across switches, including EXC_RETURN and FP callee-saved state when required.
- Cortex-M critical sections preserve stricter pre-existing `BASEPRI` masks and restore the exact outer entry state.
- Mutexes are task-context-only and currently have no priority inheritance or timed lock. Tasks must release owned mutexes before return/delete; owner-death recovery is not provided in v0.5.
- Semaphores and queues expose ISR operations. Wake decisions follow the active scheduler policy rather than treating every wake as immediate preemption.
- Event flags, task notifications and IPC timeout variants are not present yet.

## Accepted v0.5 development hardware baseline

Exact `develop` SHA `80f2042f2c64053a9ea888666474c5dad5f72797` passed the full NUCLEO-H755ZI-Q CM7 development qualification:

```text
Board probe:          PASS
Functional contracts: 11 / 11 PASS
Hardware benchmarks:  22 / 22 PASS
Overall:               PASS
```

This closes the scheduler/lifecycle hardening phase at the development level. The final v0.5.0 release candidate must repeat the same unfiltered hardware suite on the exact frozen RC SHA.

Broader hard-real-time work such as priority-inversion mitigation, maximum critical-section characterization, queue-copy scaling, interference analysis and machine-readable timing evidence remains open for later qualification and is not treated as a completed v0.5 scheduler-hardening item.

Bundled C and C++ examples are available for tasks, semaphores, counting semaphores, mutexes and queues.
