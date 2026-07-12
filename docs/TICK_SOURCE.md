# Tick Sources

HardRT v0.4.0 supports a port-owned periodic tick and an application-owned external tick. Both modes use the same core tick-accounting function, but they enter it through different interfaces.

## Port-owned tick

`HRT_TICK_SYSTICK` is the default.

The selected port starts its timer from `hrt_port_start_systick()` and calls the private core handler from the timer interrupt or hosted tick callback:

```c
hrt__tick_isr();
```

This is the path used by the current Cortex-M SysTick handler and POSIX `SIGALRM` handler.

Application code must not call `hrt_tick_from_isr()` in this mode. The current implementation ignores such a call and does not advance the tick counter.

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

    hrt_init(&cfg);
    hrt_start();
}
```

The application timer ISR calls the public API once per configured kernel tick:

```c
#include "hardrt_time.h"

void MyTimer_IRQHandler(void) {
    /* Clear the timer interrupt source first. */
    hrt_tick_from_isr();
}
```

In external mode, `hrt_port_start_systick()` must not start its own periodic source. The current Cortex-M port still configures PendSV and enables interrupts; the POSIX port returns without installing `SIGALRM`.

## What tick processing does

Each accepted tick:

1. increments the 32-bit tick counter;
2. wakes sleeping tasks whose deadlines have expired;
3. decrements the current task's non-zero slice under `HRT_SCHED_RR` or `HRT_SCHED_PRIORITY_RR`;
4. requests a context switch when a task was awakened or a slice reached zero.

Tick processing does not switch task contexts directly. Cortex-M performs the eventual transfer through PendSV. POSIX performs it only after a task returns to the scheduler through a HardRT scheduling point.

The timer ISR does not need to call a separate `yield_from_isr` function. No such public API exists in v0.4.0.

## Tick frequency and millisecond conversion

`tick_hz` defines the conversion between milliseconds and ticks. `hrt_sleep()` uses ceiling division, so a positive duration shorter than one tick sleeps for one tick.

In the current implementation, `hrt_sleep(0)` also sleeps for one tick. Use `hrt_yield()` for an immediate voluntary scheduling point.

## Wraparound

The tick counter is a `uint32_t` and wraps naturally. Sleeping-task deadlines are compared using signed subtraction, which supports intervals shorter than half of the 32-bit tick range.