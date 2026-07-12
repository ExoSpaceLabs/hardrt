# Module Status

Current components in HardRT v0.4.0:

- [`inc/hardrt.h`](../inc/hardrt.h): task, scheduler, configuration, time-query, version, port-identity, and currently exposed internal declarations.
- [`inc/hardrt_time.h`](../inc/hardrt_time.h): public external-tick entry point `hrt_tick_from_isr()`.
- [`inc/hardrt_sem.h`](../inc/hardrt_sem.h): binary/counting semaphores and ISR give.
- [`inc/hardrt_mutex.h`](../inc/hardrt_mutex.h): owner-tracked non-recursive mutexes.
- [`inc/hardrt_queue.h`](../inc/hardrt_queue.h): fixed-size copy-based queues with task and ISR operations.
- [`inc/hardrt_port_int.h`](../inc/hardrt_port_int.h): non-installed port/core interface used by reference ports.
- [`cpp/hardrtpp.hpp`](../cpp/hardrtpp.hpp): optional header-only C++17 wrappers.
- generated `hardrt_version.h`: project version metadata.
- generated `hardrt_port.h`: selected port identity.

## Current status notes

- Version: `0.4.0`.
- Priority values `HRT_PRIO0` through `HRT_PRIO11` are declared; the build-time usable range is `0` through `HARDRT_MAX_PRIO - 1`.
- CMake defines `HARDRT_MAX_TASKS` as `HARDRT_CFG_MAX_TASKS + 1` to include the idle slot.
- Ready queues are FIFO per priority. All current scheduler policies select the highest non-empty priority queue.
- `HRT_SCHED_RR` and `HRT_SCHED_PRIORITY_RR` enable non-zero slice accounting; global priority-independent round-robin is not implemented.
- The port-owned tick path calls private `hrt__tick_isr()`.
- The public `hrt_tick_from_isr()` API is accepted only in `HRT_TICK_EXTERNAL` mode.
- POSIX uses Linux/glibc `ucontext`, signal-driven tick accounting, and cooperative context handoff at HardRT scheduling points.
- Mutexes are task-context-only and have no priority inheritance or timed lock.
- Semaphores and queues expose ISR producer/consumer operations. Their `need_switch` output currently indicates that a waiter was awakened, not that a priority comparison selected immediate preemption.
- Event flags, task notifications, and IPC timeout variants are not present in the current codebase.

Bundled C and C++ examples are available for tasks, semaphores, counting semaphores, mutexes, and queues.