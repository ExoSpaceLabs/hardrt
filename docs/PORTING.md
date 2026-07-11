# HardRT Porting Guide

This guide describes the port interface used by HardRT v0.4.0 on the `develop` branch. It documents the hooks that the current core and reference ports actually call. It does not describe planned v0.5.0 interfaces.

HardRT separates the portable scheduler and synchronization code from architecture-specific tick, critical-section, stack, idle, and context-switch operations.

## Current execution model

- The core owns task state and scheduling decisions.
- A port owns stack-frame construction and context transfer.
- Tick handlers update kernel state and request rescheduling; they do not directly run application task code.
- Blocking APIs transfer control back to the scheduler from task context.
- All storage is static.

## Hooks implemented by a port

### `void hrt_port_start_systick(uint32_t tick_hz)`

Called by `hrt_init()`.

Responsibilities:

- start the port-owned periodic tick when `HRT_TICK_SYSTICK` is selected;
- leave the periodic tick disabled when `HRT_TICK_EXTERNAL` is selected;
- configure any context-switch interrupt required by the port;
- make the requested tick frequency as accurate as the platform permits.

The current `hrt_init()` call order invokes this hook before `hrt__init_idle_task()`. A port must not enter the scheduler or dispatch a task from this hook. The scheduler is entered later by `hrt_start()`.

For a port-owned tick, the port handler calls the private core function `hrt__tick_isr()`. It does not call the public external-tick API.

### `void hrt_port_enter_scheduler(void)`

Called by `hrt_start()` after the core requests an initial reschedule.

- Cortex-M enables interrupts, pends PendSV, and remains in its idle loop.
- POSIX enters the hosted scheduler loop and transfers between `ucontext` objects.
- The null port returns without running tasks.

### `void hrt_port_yield_to_scheduler(void)`

Called from task context by sleep, yield, task deletion, and blocking synchronization paths.

- Cortex-M pends PendSV; the context change occurs through exception handling.
- POSIX swaps from the current task context to the scheduler context with `SIGALRM` masked.

This hook is task-context-only.

### `void hrt__pend_context_switch(void)`

Requests a context switch at the port's next safe scheduling point.

- Cortex-M sets `PENDSVSET`.
- POSIX sets a `sig_atomic_t` pending flag.

The current core calls this function from task paths and tick/ISR-style paths. It must be safe to call repeatedly.

### `void hrt_port_idle_wait(void)`

Called when no application task is selected.

- Cortex-M executes `WFI`.
- POSIX performs a short `nanosleep()` to avoid a busy loop.

### `void hrt_port_prepare_task_stack(int id, void (*tramp)(void), uint32_t *stack_base, size_t words)`

Builds or records the initial execution context for a task.

The current Cortex-M port creates an exception-return frame and saves the resulting stack pointer in the task control block. The POSIX port initializes a `ucontext_t` using the application-provided stack buffer.

The stack must remain valid for the task lifetime. `hrt_create_task()` currently rejects stacks smaller than 64 words before this hook is called.

### `void hrt__task_trampoline(void)`

Starts the current task's entry function with its stored argument. If the task returns, both current reference ports call `hrt_task_delete()`. Returning tasks are therefore marked unused and are not requeued.

### `void hrt_port_crit_enter(void)` / `void hrt_port_crit_exit(void)`

Protect scheduler and synchronization data.

The current core uses these hooks from ordinary task APIs and from the semaphore/queue `_from_isr` paths. A compatible port must therefore support the contexts in which its ISR-facing APIs are permitted.

Requirements of the current implementation:

- nesting must work;
- the outermost exit restores or releases the protection;
- scheduler data must not be observed partially updated;
- the implementation must not mask faults that are required for platform safety.

The Cortex-M port uses `BASEPRI`. Its defaults are:

```c
HARDRT_NVIC_PRIO_BITS       = 4
HARDRT_MAX_SYSCALL_IRQ_PRIO = 5
```

An interrupt that calls HardRT ISR APIs must be configured at a priority compatible with that `BASEPRI` ceiling. Numerically lower Cortex-M priority values are more urgent and may not be masked by the critical section.

The POSIX port blocks `SIGALRM` and uses a nesting counter.

### `void hrt_port_sp_valid(uintptr_t sp)`

Performs port-specific stack-pointer validation. The Cortex-M implementation checks RAM range and 8-byte alignment. The POSIX implementation currently accepts the supplied value.

### `void hrt__init_idle_task(void)`

Initializes the port's idle representation.

The Cortex-M port constructs the idle task context. The POSIX port currently leaves this hook empty and handles idle waiting in the scheduler loop.

## Core-private functions used by ports

The current private port header and source files use functions including:

```c
uint32_t          hrt__cfg_core_hz(void);
hrt_tick_source_t hrt__cfg_tick_src(void);
uint32_t          hrt__cfg_tick_hz(void);
int               hrt__get_current(void);
int               hrt__pick_next_ready(void);
void              hrt__save_current_sp(uintptr_t sp);
uintptr_t         hrt__load_next_sp_and_set_current(int next_id);
void              hrt__tick_isr(void);
```

Some declarations are still repeated directly in port source files in v0.4.0. A new port must follow the current implementation and `inc/hardrt_port_int.h`; these symbols are not application APIs.

## Tick handling

### Port-owned tick

When `HRT_TICK_SYSTICK` is selected, the port's timer handler calls:

```c
hrt__tick_isr();
```

The core handler:

- increments the tick counter;
- wakes expired sleeping tasks;
- decrements the current task's non-zero timeslice under RR-enabled policies;
- pends a context switch only when a wake-up or slice expiry requires rescheduling.

### Application-owned external tick

When `HRT_TICK_EXTERNAL` is selected, the application timer ISR calls:

```c
hrt_tick_from_isr();
```

That public function delegates to the same core tick path. If it is called while the configured source is not `HRT_TICK_EXTERNAL`, the current implementation returns without advancing time.

The caller does not need to invoke a second yield function. Tick processing requests rescheduling internally when required.

## Context switching

### Cortex-M

The current Cortex-M port:

- uses PSP for task stacks;
- saves/restores `r4-r11` in PendSV assembly;
- relies on the hardware exception frame for the remaining basic registers;
- requires 8-byte stack alignment;
- sets PendSV below SysTick in interrupt priority.

The current implementation does not document or implement floating-point context preservation as part of the task-switch contract.

### POSIX

The POSIX port uses `ucontext` on Linux/glibc and `SIGALRM` for tick accounting.

The signal handler calls `hrt__tick_isr()` and sets a pending flag. It does not call `swapcontext()`. A task reaches the scheduler only through a HardRT scheduling point such as:

- `hrt_yield()`;
- `hrt_sleep()`;
- blocking semaphore, mutex, or queue operations;
- `hrt_task_delete()`;
- returning from the task function.

For that reason, the current POSIX port provides cooperative context handoff even though tick accounting and slice expiration are signal-driven. A CPU-bound task without HardRT scheduling points can prevent other tasks from running.

`ucontext` is treated as a Linux/glibc dependency. The port is not claimed to support musl or macOS.

## Validation checklist

Before adding a port, verify at least:

- [ ] internal and external tick modes advance time exactly once per tick;
- [ ] no task context switch is performed directly by a hardware tick handler;
- [ ] task-context yield reaches the scheduler safely;
- [ ] nested critical sections preserve state;
- [ ] every ISR-facing synchronization API is called only from supported interrupt priorities;
- [ ] initial task stacks satisfy ABI alignment and frame requirements;
- [ ] task return calls `hrt_task_delete()`;
- [ ] the idle path does not consume an application ready-queue entry;
- [ ] stack and scheduler state remain valid under repeated wake, yield, block, and delete operations.

## Reference ports

- `src/port/posix/`
- `src/port/cortex_m/`
- `src/port/null/`
