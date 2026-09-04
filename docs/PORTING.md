# HardRT Porting Guide

This guide describes the port interface used by the current HardRT implementation on the `develop` branch. The authoritative non-installed declaration surface is [`src/internal/hardrt_port_contract.h`](../src/internal/hardrt_port_contract.h).

HardRT separates the portable scheduler and synchronization code from architecture-specific tick, critical-section, stack, idle, and context-switch operations.

## Contract rules

- A port includes `hardrt_port_contract.h`; it does not redeclare kernel-private functions locally.
- The contract header declares every hook a port must implement and identifies the core-private services a port is allowed to call.
- Private port/kernel headers live under `src/internal` and are not part of an installed HardRT package.
- `tests/port_contract_fixture.c` is a compile-only skeleton implementation. Changing a required hook without updating the fixture is a build failure.
- Cortex-M ports intended for hard-real-time qualification must give every timing-sensitive hook bounded behavior under the supported configuration. POSIX follows the logical contract but is not timing-qualified.

## Current execution model

- The core owns task state and scheduling decisions.
- A port owns stack-frame construction and context transfer.
- `hrt_init()` initializes all core state and the idle representation before the port configures its tick mechanism.
- Tick configuration during `hrt_init()` must remain inactive. A port activates its internal periodic tick only when `hrt_start()` crosses the scheduler-start boundary.
- Tick handlers update kernel state and request rescheduling; they do not directly run application task code.
- Blocking APIs transfer control back to the scheduler from task context.
- Kernel runtime storage is static.

## Scheduler ownership and READY transitions

The core owns the outgoing READY-task transition exactly once before a successor is selected. A port must not separately enqueue, rotate, or refresh a task merely because it is performing the architecture-specific context switch.

Current `develop` semantics are:

- blocked, sleeping, deleted, or returned tasks are not requeued;
- explicit `hrt_yield()` rotates the current READY task to the tail once and refreshes its quantum;
- RR quantum expiry rotates the task to the tail once and refreshes the next quantum;
- higher-priority asynchronous preemption preserves the interrupted task's queue precedence and remaining quantum;
- ISR/tick code requests a switch but never directly enters application task context.

Wake paths use one scheduler-aware decision. Under `HRT_SCHED_PRIORITY` and `HRT_SCHED_PRIORITY_RR`, a newly READY task requests immediate scheduling only when it has strictly higher priority than the current READY task, or when no normal READY task is running. Equal- and lower-priority wakes do not force a context switch solely because they became READY.

This rule is also the meaning of public ISR `need_switch` outputs. The ISR API itself requests the switch when required; applications do not call a second ISR-yield hook.

See [SCHEDULING.md](SCHEDULING.md) for the complete contract and the retained-quantum validation trace.

## Hooks implemented by a port

### `hrt_port_configure_tick(uint32_t tick_hz)`

Caller: `hrt_init()` after core scheduler storage and the idle representation are complete. Context: non-ISR. It must not block, dispatch a task, activate a periodic interrupt, or globally enable interrupts.

For `HRT_TICK_SYSTICK`, this hook configures the port-owned timer and context-switch priorities but leaves the timer inactive. For `HRT_TICK_EXTERNAL`, it configures only the scheduler mechanism required by the port and leaves timer ownership entirely with the application. It returns zero on success and a negative value when the requested internal tick cannot be represented by the port.

### `hrt_port_enter_scheduler()`

Caller: `hrt_start()`. This hook owns the ordered scheduler-start boundary. Architecture state required before task execution must be completed first; the configured internal periodic tick is then activated, the first scheduling decision is requested, and task execution is allowed to begin.

On Cortex-M, startup is briefly protected with interrupts masked while FP context support is configured, the initial PendSV is pended, and SysTick is armed. Interrupts are enabled only after that sequence is complete. POSIX arms `SIGALRM` only after selecting the first valid current task and then enters the hosted scheduler loop. The null port returns without executing tasks.

### `hrt_port_yield_to_scheduler()`

Caller: task-context sleep, yield, delete, and blocking synchronization paths. Task-context-only. It may transfer control immediately or pend an architecture-specific switch, but it is never the ISR reschedule API. The port must not add an additional ready-queue transition; the core scheduler owns that decision.

### `hrt__pend_context_switch()`

Caller: task paths and supported ISR/tick paths after scheduler startup. It must be non-blocking, ISR-safe for the configured kernel-aware interrupt range, and safe to invoke repeatedly. Cortex-M sets `PENDSVSET`; POSIX sets its pending scheduler flag. It requests a switch but does not directly run a task from ISR context.

The initial scheduler handoff is owned by `hrt_port_enter_scheduler()` rather than by a separate core-side pend before port startup. This prevents the first Cortex-M PendSV from running before architecture startup state is ready.

### `hrt_port_idle_wait()`

Caller: scheduler/idle context. Cortex-M executes `WFI`; POSIX uses a short sleep. It must not alter scheduler queues.

### `hrt_port_prepare_task_stack(int id, void (*tramp)(void), uint32_t *stack_base, size_t words)`

Caller: `hrt_create_task()` before the task becomes READY. It builds or records the initial execution context. The stack remains application-owned for the task lifetime. The operation must not block. It returns zero only when the context is fully usable; a negative return causes task creation to roll the slot back to `HRT_UNUSED`.

### `hrt__task_trampoline()`

Runs on first task entry. It invokes the current task function with its stored argument and calls `hrt_task_delete()` if that function returns.

### `hrt_port_crit_enter()` / `hrt_port_crit_exit()`

Caller: scheduler/synchronization code in task context and supported kernel-aware ISR APIs. The pair must be non-blocking and nestable. The outermost exit restores the previous protection state. No partially updated scheduler state may be exposed.

The Cortex-M reference port uses `BASEPRI` with current defaults:

```c
HARDRT_NVIC_PRIO_BITS       = 4
HARDRT_MAX_SYSCALL_IRQ_PRIO = 5
```

Any IRQ calling HardRT ISR APIs must obey the configured syscall-priority ceiling. The duration of these masked regions is part of the hard-real-time qualification work in #53.

### `hrt_port_sp_valid(uintptr_t sp)`

Port-specific stack-pointer validation. Cortex-M checks RAM range and architecture-required alignment; POSIX currently accepts the value. It must not block.

### `hrt__init_idle_task()`

Initializes the port's idle representation during `hrt_init()`. It is completed before `hrt_port_configure_tick()` is called, so no configured or external tick path can observe partially initialized idle state as a consequence of HardRT initialization ordering.

## Core-private services available to ports

The allowed current port-to-core surface is declared through the contract header and includes:

```c
uint32_t          hrt__cfg_core_hz(void);
hrt_tick_source_t hrt__cfg_tick_src(void);
uint32_t          hrt__cfg_tick_hz(void);
void              hrt__tick_isr(void);
int               hrt__get_current(void);
void              hrt__set_current(int id);
int               hrt__pick_next_ready(void);
void              hrt__on_scheduler_entry(void);
uintptr_t         hrt__schedule(uintptr_t old_sp);
```

The current reference ports also require private TCB/context helpers for task trampoline and stack setup. They are deliberately private and may be narrowed further without changing the application ABI.

## Tick handling

### Port-owned tick

With `HRT_TICK_SYSTICK`, `hrt_init()` configures but does not activate the port timer. `hrt_start()` crosses the activation boundary through `hrt_port_enter_scheduler()`. Once active, the timer handler calls private `hrt__tick_isr()`. That increments time, wakes expired sleepers, accounts for RR slices, and requests a reschedule when required. A tick handler never directly switches application context.

### Application-owned external tick

With `HRT_TICK_EXTERNAL`, HardRT never starts a timer. The application timer ISR calls public `hrt_tick_from_isr()`, which reaches the same core tick path. Calling it in internal-tick mode is currently ignored; #27 tracks tightening that misuse contract.

Because the external timer is application-owned, applications are responsible for not invoking its HardRT tick path before `hrt_init()` has completed. HardRT itself no longer enables global interrupts as a side effect of `hrt_init()`.

## Context switching

### Cortex-M

The reference Cortex-M port uses PSP task stacks, PendSV, hardware exception frames, software save/restore of `r4-r11`, per-context EXC_RETURN on hard-float builds, and conditional save/restore of `s16-s31` when an extended FP frame is active. PendSV is lower priority than SysTick. FP context support is configured before the first PendSV can dispatch a task.

The physically qualified PRIORITY_RR ordering is `low-A -> ISR/wake -> high -> low-A -> low-B`, with the interrupted low-A retaining its unused quantum across the higher-priority dispatch.

### POSIX

The POSIX port uses `ucontext` and `SIGALRM`. `hrt_init()` installs/configures the signal source but leaves the interval timer disarmed. Scheduler entry arms it only after selecting a valid current task. The signal handler performs tick accounting and requests scheduling but never calls `swapcontext()`. A task returns to the hosted scheduler only at a HardRT scheduling point, so this port is functional/cooperative rather than a timing-accurate Cortex-M model.

## Validation checklist

Before adding a port, verify at least:

- [ ] the port compiles using only `hardrt_port_contract.h` plus platform headers;
- [ ] `hrt_init()` cannot activate the periodic tick or globally enable interrupts;
- [ ] idle/core state is complete before tick configuration;
- [ ] scheduler entry activates the internal tick and performs the first dispatch in a defined order;
- [ ] internal and external tick modes advance time exactly once per tick;
- [ ] no task context switch occurs directly in a hardware tick handler;
- [ ] task-context yield reaches the scheduler safely and rotates exactly once;
- [ ] higher-priority preemption does not rotate the interrupted task behind equal-priority peers;
- [ ] nested critical sections preserve prior protection state;
- [ ] ISR-facing APIs are called only from supported interrupt priorities;
- [ ] initial stacks satisfy the target ABI and alignment requirements;
- [ ] task return reaches `hrt_task_delete()`;
- [ ] idle does not enter an application ready queue;
- [ ] repeated wake, yield, block, and delete operations preserve unique READY membership.

## Reference ports

- `src/port/posix/`
- `src/port/cortex_m/`
- `src/port/null/`
