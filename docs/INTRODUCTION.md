# Introduction to HardRT

HardRT is a small real-time operating-system kernel written in C for statically bounded embedded systems and hosted functional/scheduler testing.

## Scope

HardRT 0.5.0 provides:

- static application-owned task stacks and bounded task capacity;
- fixed-priority, global round-robin, and fixed-priority round-robin scheduling;
- runtime task creation with EXITED-slot reclamation;
- sleep, yield, deletion/return, tick/time queries, and runtime scheduler tuning;
- binary and counting semaphores;
- owner-tracked non-recursive mutexes;
- fixed-capacity message queues;
- 32-bit event flags;
- per-task 32-bit notifications;
- task and ISR producer operations where documented;
- null, POSIX, and Cortex-M ports;
- optional allocation-free C++17 wrappers.

It intentionally does not provide a heap, filesystem, networking stack, device HAL, process isolation, or general-purpose operating-system services.

Generic IPC timeouts, mutex priority inheritance/owner-death recovery, tickless idle, and high-resolution timers are not part of v0.5.0.

## Design goals

| Goal | Current approach |
|---|---|
| Small core | Task, scheduler, timing, and synchronization logic remain in a compact C implementation. |
| Static allocation | Task stacks and queue/event storage are static or application-owned. |
| Port separation | Context, tick, critical-section, idle, and architecture details live under `src/port/`. |
| Determinism | READY/sleeper/waiter storage is bounded and scheduler/wake semantics are explicit. |
| Bounded storage | Kernel/object storage is sized by compile-time task/priority limits. |
| Reproducibility | Cortex-M timing claims are tied to explicit hardware/build/runtime evidence. |

Static allocation and bounded data structures do not themselves prove application deadlines. Blocking synchronization calls can wait indefinitely, continuously READY higher-priority tasks can starve lower-priority work, event-set cost depends on registered waiter count, and queue critical-section cost depends on payload size.

## Scheduling model

Priority zero is highest.

- `HRT_SCHED_PRIORITY` uses strict fixed-priority FIFO scheduling.
- `HRT_SCHED_RR` uses one global FIFO and ignores task priority.
- `HRT_SCHED_PRIORITY_RR` uses fixed-priority selection with round-robin rotation only within a priority class.
- `timeslice == 0` disables tick-driven rotation for that task.

Higher-priority preemption under `HRT_SCHED_PRIORITY_RR` preserves the interrupted task's queue precedence and unused quantum. ISR `need_switch` results follow the active scheduler policy rather than merely reporting that a waiter was awakened.

The Cortex-M port performs context transfer through PendSV. The POSIX port uses signal-driven tick accounting but transfers task context only when the running task reaches a HardRT scheduling point; it is not a timing-accurate Cortex-M model.

## Typical uses

- Cortex-M firmware with statically bounded task/synchronization storage;
- spacecraft subsystem and payload-control prototypes;
- deterministic scheduler/IPC experiments;
- POSIX-hosted functional tests before target integration.

## Further reading

- [Build guide](BUILD.md)
- [C API](API_C.md)
- [C++ wrapper](CPP.md)
- [Events and task notifications](EVENTS_NOTIFICATIONS.md)
- [Scheduling](SCHEDULING.md)
- [Porting guide](PORTING.md)
- [Tick sources](TICK_SOURCE.md)
- [Compatibility policy](COMPATIBILITY.md)
- [Roadmap](ROADMAP.md)
