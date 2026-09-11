# Introduction to HardRT

HardRT is a small real-time operating-system kernel written in C for statically bounded embedded systems and hosted functional/scheduler testing.

The Cortex-M target is being developed toward an **explicitly qualified hard real-time RTOS contract**. The goal is not merely preemption or low average latency: supported configurations must have deterministic scheduling and synchronization semantics, statically bounded kernel behavior, bounded priority inversion and critical sections, periodic timing primitives, and reproducible timing evidence suitable for schedulability analysis. Until those bounds are established, current releases distinguish measured configuration-specific evidence from universal WCET guarantees.

## Scope

HardRT 0.5.1 provides:

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

Generic IPC timeouts, bounded mutex priority-inversion handling, owner-death recovery, absolute periodic release timing, tickless idle, and high-resolution timers are not part of v0.5.1.

## Design goals

| Goal | Current approach |
|---|---|
| Small core | Task, scheduler, timing, and synchronization logic remain in a compact C implementation. |
| Static allocation | Task stacks and queue/event storage are static or application-owned. |
| Port separation | Context, tick, critical-section, idle, and architecture details live under `src/port/`. |
| Determinism | READY/sleeper/waiter storage is bounded and scheduler/wake semantics are explicit. |
| Bounded storage | Kernel/object storage is sized by compile-time task/priority limits. |
| Hard-real-time progression | 0.6 adds bounded synchronization/periodic semantics; 0.7 deepens timing/interference qualification; 0.8 hardens the pre-1 API/kernel; 0.9 freezes and qualifies the 1.0 candidate. |
| Reproducibility | Cortex-M timing claims are tied to explicit hardware/build/runtime evidence. |

Static allocation and bounded data structures do not themselves prove application deadlines. Blocking synchronization calls can wait indefinitely in v0.5.1, mutexes do not yet bound priority inversion, continuously READY higher-priority tasks can starve lower-priority work, event-set cost depends on registered waiter count, and queue critical-section cost depends on payload size. These gaps are explicitly tracked on the path to 1.0 rather than hidden behind the project name.

## Scheduling model

Priority zero is highest.

- `HRT_SCHED_PRIORITY` uses strict fixed-priority FIFO scheduling.
- `HRT_SCHED_RR` uses one global FIFO and ignores task priority.
- `HRT_SCHED_PRIORITY_RR` uses fixed-priority selection with round-robin rotation only within a priority class.
- `timeslice == 0` disables tick-driven round-robin rotation for that task; it does not disable scheduler-policy preemption.

Higher-priority preemption under `HRT_SCHED_PRIORITY_RR` preserves the interrupted task's queue precedence and unused quantum. ISR `need_switch` results follow the active scheduler policy rather than merely reporting that a waiter was awakened.

The Cortex-M port performs context transfer through PendSV. The 0.5.1 POSIX port maps HardRT application tasks to pthreads, uses a monotonic timer pthread for an internal tick, and uses targeted signals to park/resume the selected hosted task so CPU-bound code can be preempted without first entering a HardRT API. The common HardRT core remains authoritative for task state and scheduling policy. POSIX is a functional/scheduler-validation environment, not a timing-accurate Cortex-M model or a hard-real-time target.

The hosted POSIX port currently reserves process-wide `SIGALRM` and `SIGUSR2`. The application-provided HardRT task-stack buffer remains part of task lifetime and overlap validation, but it is not used as the native pthread execution stack.

## Path to 1.0

The release progression is maintained in [ROADMAP.md](ROADMAP.md) and the qualification umbrella #48:

- **0.6.x:** bounded IPC timeouts, bounded mutex priority inversion, absolute periodic timing, and synchronization-critical timing decisions;
- **0.7.x:** timing decomposition, release jitter, IRQ/task interference, machine-readable evidence, and configuration-specific bounds;
- **0.8.x:** pre-1 API/kernel hardening, architecture-neutral interfaces, memory accounting, and static-analysis depth;
- **0.9.x:** frozen 1.0 API/configuration candidate and complete qualification campaign;
- **1.0.0:** stable, explicitly qualified hard-real-time contract for documented Cortex-M configurations.

SVC/MPU privilege separation is a separately tracked optional architecture direction. PendSV remains the Cortex-M context-switch mechanism.

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