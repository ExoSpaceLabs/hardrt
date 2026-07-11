# Introduction to HardRT

HardRT is a small real-time operating-system kernel written in C for static embedded systems and hosted logic testing.

## Scope

HardRT v0.4.0 provides:

- static task creation;
- priority-based scheduling;
- optional tick-driven rotation within priority classes;
- sleep and yield operations;
- binary and counting semaphores;
- owner-tracked mutexes;
- fixed-size message queues;
- null, POSIX, and Cortex-M ports.

It intentionally does not provide a heap, filesystem, networking stack, device HAL, process isolation, or general-purpose operating-system services.

## Design goals

| Goal | Current approach |
|---|---|
| Small core | Task, scheduler, timing, and synchronization logic are kept in a limited set of C sources. |
| Static allocation | Task stacks and queue storage are supplied at build time or by the application. |
| Port separation | Architecture-specific context, tick, critical-section, and idle operations live under `src/port/`. |
| Inspectability | Public structures and much of the current kernel state are directly visible in the source. |
| Bounded storage | Kernel arrays are sized by compile-time task and priority limits. |

Static allocation and bounded data structures do not by themselves prove an application's deadlines. Blocking semaphore, mutex, and queue calls may wait indefinitely, lower-priority tasks may be starved by continuously ready higher-priority work, and queue copy time depends on item size.

## Scheduling model

Priority zero is highest. The current scheduler stores ready tasks in FIFO queues per priority and chooses from the highest non-empty queue.

`HRT_SCHED_PRIORITY` does not use tick-driven slices. `HRT_SCHED_RR` and `HRT_SCHED_PRIORITY_RR` both account non-zero slices, but v0.4.0 still performs priority-ordered task selection in both modes.

The Cortex-M port can transfer context through PendSV after a tick or ISR requests rescheduling. The POSIX port uses signal-driven tick accounting but transfers task context only when the running task reaches a HardRT scheduling point.

## Typical uses

- Cortex-M firmware with statically defined tasks;
- subsystem and payload-control prototypes;
- educational inspection of scheduler and synchronization mechanisms;
- POSIX-hosted functional tests before target integration.

The POSIX port is not a cycle-accurate or timing-accurate representation of Cortex-M execution.

## Further reading

- [Build guide](BUILD.md)
- [C API](API_C.md)
- [C++ wrapper](CPP.md)
- [Porting guide](PORTING.md)
- [Tick sources](TICK_SOURCE.md)
- [Roadmap](ROADMAP.md)