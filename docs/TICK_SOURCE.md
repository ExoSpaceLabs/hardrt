# Tick Sources

HardRT supports two tick-ownership modes. Both ultimately use the same private core tick-accounting path, but ownership and entry points are deliberately different so one hardware event cannot be counted twice.

## Lifecycle rule

`hrt_init()` initializes scheduler storage and the private idle task first, then calls the port's `hrt_port_configure_tick()` hook. Configuration must remain inactive: it must not start a periodic interrupt, dispatch a task, or globally enable interrupts.

`hrt_start()` crosses the scheduler-start boundary through `hrt_port_enter_scheduler()`. Only there may a port activate its configured internal periodic tick and allow task execution to begin.

This ordering prevents a timer IRQ or the first context switch from observing partially initialized kernel state.

## Port-owned tick

`HRT_TICK_SYSTICK` is the default.

The selected port configures its periodic source during `hrt_init()` but leaves it disabled. Scheduler entry activates that source. Once active, the port-owned timer handler calls the private core hook:

```c
hrt__tick_isr();
```

This is the path used by the Cortex-M `SysTick_Handler` and the POSIX `SIGALRM` handler.

Application code must not call `hrt_tick_from_isr()` in this mode. If it does, HardRT leaves tick accounting unchanged and records `ERR_TICK_SOURCE_MISMATCH` through the kernel diagnostic path.

On Cortex-M, scheduler entry briefly masks interrupts while architecture startup state is finalized, the initial PendSV is requested, and SysTick is armed. Interrupts are enabled only after that ordered sequence is complete.

On POSIX, the interval timer remains disarmed until the hosted scheduler has selected a valid current task.

## Application-owned external tick

Select external mode during initialization:

```c
#include "hardrt.h"

int main(void) {
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_EXTERNAL
    };

    if (hrt_init(&cfg) != HRT_OK) {
        return 1;
    }
    hrt_start();
}
```

HardRT does not start a periodic timer in external mode. The application-owned timer ISR calls the public API exactly once per configured kernel tick:

```c
#include "hardrt_time.h"

void MyTimer_IRQHandler(void) {
    /* Clear the timer interrupt source first. */
    hrt_tick_from_isr();
}
```

The application owns the external timer lifecycle. It may configure the peripheral before `hrt_start()`, but it must not enable or route periodic tick interrupts into HardRT until the scheduler is RUNNING and has selected a valid current task. On Cortex-M, a robust pattern is to enable the external timer from the first application task that runs.

## What tick processing does

Each accepted tick:

1. increments the 32-bit tick counter;
2. advances the intrusive delta sleep queue and wakes every task whose relative deadline expires;
3. decrements the current task's non-zero slice under `HRT_SCHED_RR` or `HRT_SCHED_PRIORITY_RR`;
4. requests a context switch when a wake or slice expiry requires scheduling.

Tick processing does not directly execute another application task. Cortex-M performs the eventual transfer through PendSV. POSIX records a pending scheduling request and transfers only when control returns to the hosted scheduler.

The timer ISR does not call a separate `yield_from_isr` function. No such public API is required by the current contract.

## Tick frequency

`tick_hz` defines the conversion between milliseconds and ticks. `hrt_sleep()` uses ceiling division, so a positive duration shorter than one tick sleeps for one tick.

An internal tick configuration is rejected when the selected port cannot represent the requested period. For example, the Cortex-M SysTick implementation rejects reload counts outside its 24-bit range instead of silently clamping them.

`hrt_sleep(0)` is an immediate scheduling point equivalent to `hrt_yield()` and does not consume a tick.

## Wraparound

The public tick counter is a `uint32_t` and wraps naturally. Sleeping tasks are stored in an intrusive delta queue using relative intervals rather than absolute-deadline comparisons. The queue therefore continues to count down correctly across public tick-counter wrap, provided an individual requested sleep interval fits the supported `uint32_t` tick duration.
