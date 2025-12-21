# Porting HardRT

This document explains what a port must implement and the rules for interacting with the core scheduler and time subsystem.

## Required hooks (implemented by the port)

- `void hrt_port_start_systick(uint32_t hz);`
  - Start a periodic tick at `hz` and, on each tick, call `hrt_tick_from_isr()` from the timer ISR/thread.
- `void hrt_port_idle_wait(void);`
  - Optional low-power or sleep while idle. It may be a short sleep on hosted ports.
- `void hrt__pend_context_switch(void);`
  - Request a context switch from an ISR-safe context. Do not perform the switch here; set a flag or trigger a deferred mechanism (e.g., PendSV on Cortex-M).
- `void hrt__task_trampoline(void);`
  - Architecture-specific entry trampoline is used as the first function for new tasks.
- `void hrt_port_prepare_task_stack(int id, void (*tramp)(void), uint32_t* stack_base, size_t words);`
  - Lay out the initial stack/context for task `id` so that switching to it will execute `tramp`.

Additionally, a port that implements an actual scheduler loop must provide:

- `void hrt_port_enter_scheduler(void);`
  - The main scheduler loop that picks the next READY task and switches to it.
- `void hrt_port_yield_to_scheduler(void);`
  - A task-context function that switches back into the scheduler loop.

## Core-provided internal helpers a port may call

- `void hrt_tick_from_isr(void);`
  - Called by the port on every tick. It advances time, wakes sleepers, and performs time-slice accounting for the currently running task. It is ISR-safe and must not perform a context switch.
- `void hrt__on_scheduler_entry(void);`
  - Call this right after returning from a task back to the scheduler context (with interrupts/tick masked as needed). It rotates a time-slice-expired task to the tail of its ready queue and refreshes its quantum.
- `int hrt__pick_next_ready(void);`
  - Pick the next READY task according to the current scheduling policy (RR/PRIORITY/PRIORITY_RR).
- `void hrt__set_current(int id);`
  - Inform the core which task is now running.

## Important rules

- Do not context-switch from an ISR or signal handler. From the tick ISR, only call `hrt_tick_from_isr()` and then request a rescheduling via `hrt__pend_context_switch()`.
- Mask/disable the tick around raw context switches if your platform can receive ticks during a swap to avoid reentrancy.
- For round-robin policies, invoke `hrt__on_scheduler_entry()` upon re-entering the scheduler from a task to apply time-slice rotation safely.

## Reference ports

- `posix` (Linux hosted): uses `setitimer` + `SIGALRM` for ticks and `ucontext` for cooperative switching. The signal handler calls `hrt_tick_from_isr()` and sets a rescheduling flag. Rotation is applied in the scheduler loop via `hrt__on_scheduler_entry()` with SIGALRM masked.
- `null`: a stub that provides the required hooks but does not start a tick or switch context. Useful for build-time validation only.