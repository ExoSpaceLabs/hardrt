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
- Tick handlers update kernel state and request rescheduling; they do not directly run application task code.
- Blocking APIs transfer control back to the scheduler from task context.
- Kernel runtime storage is static.

## Hooks implemented by a port

### `hrt_port_start_systick(uint32_t tick_hz)`

Caller: `hrt_init()` during startup. Context: non-ISR. It must not block or dispatch a task. It configures the port-owned tick when `HRT_TICK_SYSTICK` is selected, leaves it disabled for `HRT_TICK_EXTERNAL`, and configures any context-switch exception/interrupt required by the port.

The current `hrt_init()` ordering starts this hook before idle initialization; #33 tightens that lifecycle ordering. A port must not enter the scheduler from this hook.

### `hrt_port_enter_scheduler()`

Caller: `hrt_start()` after an initial reschedule request. Cortex-M enables interrupts, pends PendSV, and remains in the idle path. POSIX enters the hosted scheduler loop. The null port returns without executing tasks.

### `hrt_port_yield_to_scheduler()`

Caller: task-context sleep, yield, delete, and blocking synchronization paths. Task-context-only. It may transfer control immediately or pend an architecture-specific switch, but it is never the ISR reschedule API.

### `hrt__pend_context_switch()`

Caller: task paths and supported ISR/tick paths. It must be non-blocking, ISR-safe for the configured kernel-aware interrupt range, and safe to invoke repeatedly. Cortex-M sets `PENDSVSET`; POSIX sets its pending scheduler flag. It requests a switch but does not directly run a task from ISR context.

### `hrt_port_idle_wait()`

Caller: scheduler/idle context. Cortex-M executes `WFI`; POSIX uses a short sleep. It must not alter scheduler queues.

### `hrt_port_prepare_task_stack(int id, void (*tramp)(void), uint32_t *stack_base, size_t words)`

Caller: `hrt_create_task()` before the task becomes READY. It builds or records the initial execution context. The stack remains application-owned for the task lifetime. The operation must not block.

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

Port-specific stack-pointer validation. Cortex-M checks RAM range and 8-byte alignment; POSIX currently accepts the value. It must not block.

### `hrt__init_idle_task()`

Initializes the port's idle representation during startup. Cortex-M constructs the idle context; POSIX currently handles idle behavior in the scheduler loop and leaves this hook empty.

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

With `HRT_TICK_SYSTICK`, the port timer handler calls private `hrt__tick_isr()`. That increments time, wakes expired sleepers, accounts for RR slices, and requests a reschedule when required. A tick handler never directly switches application context.

### Application-owned external tick

With `HRT_TICK_EXTERNAL`, the application timer ISR calls public `hrt_tick_from_isr()`. It reaches the same core tick path. Calling it in internal-tick mode is currently ignored; #27 tracks tightening that misuse contract.

## Context switching

### Cortex-M

The reference Cortex-M port uses PSP task stacks, PendSV, hardware exception frames, software save/restore of `r4-r11`, and 8-byte stack alignment. PendSV is lower priority than SysTick. Floating-point context preservation is not currently part of the documented task-switch contract.

### POSIX

The POSIX port uses `ucontext` and `SIGALRM`. The signal handler performs tick accounting and requests scheduling but never calls `swapcontext()`. A task returns to the hosted scheduler only at a HardRT scheduling point, so this port is functional/cooperative rather than a timing-accurate Cortex-M model.

## Validation checklist

Before adding a port, verify at least:

- [ ] the port compiles using only `hardrt_port_contract.h` plus platform headers;
- [ ] internal and external tick modes advance time exactly once per tick;
- [ ] no task context switch occurs directly in a hardware tick handler;
- [ ] task-context yield reaches the scheduler safely;
- [ ] nested critical sections preserve prior protection state;
- [ ] ISR-facing APIs are called only from supported interrupt priorities;
- [ ] initial stacks satisfy the target ABI and alignment requirements;
- [ ] task return reaches `hrt_task_delete()`;
- [ ] idle does not enter an application ready queue;
- [ ] repeated wake, yield, block, and delete operations preserve scheduler state.

## Reference ports

- `src/port/posix/`
- `src/port/cortex_m/`
- `src/port/null/`
