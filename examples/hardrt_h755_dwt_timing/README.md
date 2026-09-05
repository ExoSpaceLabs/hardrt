# HardRT timing on STM32H755

This example collects DWT cycle-counter timing statistics from the STM32H755 CM7.
Each timing case is built as a separate image so instrumentation for one metric
does not silently contaminate another metric, because benchmarks apparently also
need protection from accidental benchmark-shaped fiction.

## Build and run

Build HardRT, the timing image, and flash the board:

```bash
scripts/build-lib-stm32h7xx-dwt-timing.sh --case event_to_task
```

Start OpenOCD:

```bash
openocd -s /usr/share/openocd/scripts \
  -f scripts/openocd_h755.cfg \
  -c "init; reset halt"
```

In another terminal, collect the result:

```bash
gdb-multiarch -q \
  examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf \
  -batch -x scripts/gdb/timing.dbg
```

The default sample count is 10,000. Override it with `--samples N`.

## Existing scheduler / semaphore cases

- `event_to_task`: legacy TIM2 ISR software timestamp to semaphore waiter
  continuation. The name predates the v0.5 event-flags API and must not be read
  as an event-flags measurement.
- `sem_isr_ready`: `hrt_sem_give_from_isr()` entry to waiter becoming READY.
- `ready_to_task`: waiter becoming READY to task continuation.
- `scheduler_decision`: diagnostic PendSV scheduler and context-switch breakdown.

## v0.5 event and notification profiling

The new signal cases use direct application-side DWT timestamps with
`HARDRT_TIMING_PROFILE=none`; production event/notification code remains
uninstrumented.

- `event_isr_to_task`: `hrt_event_set_from_isr()` entry to the higher-priority
  event waiter returning from `hrt_event_wait()`.
- `notify_isr_to_task`: `hrt_task_notify_from_isr()` entry to the
  higher-priority notification waiter returning from `hrt_task_notify_wait()`.
- `event_scan_none`: event-set ISR call cost when no registered waiter matches.
- `event_scan_one`: event-set ISR call cost when exactly one registered waiter
  matches.
- `event_scan_all`: event-set ISR call cost when every registered waiter matches.
- `notify_isr_no_wake`: notification ISR call cost when the target is running
  and no wake is required.
- `notify_isr_wake`: notification ISR call cost when a blocked notification
  waiter becomes READY.

Event scan cases accept `--waiters N`, with the qualification matrix using
`1`, `8`, `16`, and `32` actual event waiters. The build automatically sets
`HARDRT_CFG_MAX_TASKS=N+1` so one controller task remains available.

Examples:

```bash
scripts/build-lib-stm32h7xx-dwt-timing.sh \
  --case event_scan_all --waiters 32 --samples 10000

scripts/build-lib-stm32h7xx-dwt-timing.sh \
  --case event_isr_to_task --samples 10000

scripts/build-lib-stm32h7xx-dwt-timing.sh \
  --case notify_isr_wake --samples 10000
```

The event-scan firmware also verifies the number of woken waiters for every
sample. This prevents a suspiciously fast result caused by accidentally timing
a scenario that did not perform the requested work.

For toolchain and STM32 cross-compilation setup, see
[CROSSCOMPILE.md](../../docs/CROSSCOMPILE.md).
