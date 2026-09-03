# STM32H755 Manual Validation

HardRT's hosted Linux/POSIX tests are automatic. STM32H755 runtime validation is intentionally manual until a hardware CI runner exists. Cross-compilation proves that firmware links; it does not prove interrupt, PendSV, GPIO, clock, or board behavior.

Run these tests from the repository root on a NUCLEO-H755ZI-Q. The examples hold CM4 in reset and exercise CM7.

## Common requirements

- ARM GNU bare-metal toolchain (`arm-none-eabi-*`).
- OpenOCD with ST-Link support.
- A local STM32CubeH7 checkout. Set `STM32CUBE_H7_ROOT` when the default path is not valid.
- A clean working tree built from the exact HardRT commit being qualified.

All STM32 examples expose `g_example_error` for debugger inspection. A successful running example keeps it at `0`.

Failure codes used by the examples:

| Code | Meaning |
|---:|---|
| 0 | no detected example-level error |
| 1 | `hrt_init()` failed |
| 2 | first task creation failed |
| 3 | second task creation failed |
| 4 | `hrt_start()` unexpectedly returned |

A failure path executes `BKPT` and then remains in `WFI`, making the failure deterministic under a debugger.

## C blinky

Build and flash:

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-blinky.sh
```

Expected behavior:

- LD1 / PB0 toggles every 250 ms.
- LD2 / PE1 toggles every 500 ms.
- `dbg_counterA` and `dbg_counterB` increase continuously.
- `g_example_error == 0`.

PASS: observe both distinct LED rates for at least 10 seconds and, when attached with GDB, both counters continue increasing with `g_example_error == 0`.

## C++ blinky

Build and flash:

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-blinky-cpp.sh
```

Expected behavior:

- LD1 / PB0 toggles every 100 ms.
- LD2 / PE1 toggles every 250 ms.
- `dbg_counterA` and `dbg_counterB` increase continuously.
- `g_example_error == 0`.

PASS: observe both distinct LED rates for at least 10 seconds and both debugger counters advancing.

## Scheduler/demo counters

Build:

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx.sh \
  --app "$PWD/examples/hardrt_h755_demo" \
  --build-type Release
```

Flash:

```bash
openocd -s /usr/share/openocd/scripts \
  -f scripts/openocd_h755_clean.cfg \
  -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; \
      program examples/hardrt_h755_demo/build-cortex_m/hardrt_cm7_demo.elf verify; \
      reset halt; shutdown"
```

Use OpenOCD + GDB to inspect:

- `dbg_counterA`: increments before each 500 ms sleep.
- `dbg_exit_counterA`: increments after each 500 ms wake.
- `dbg_counterB`: increments before each 1000 ms sleep.
- `dbg_exit_counterB`: increments after each 1000 ms wake.
- `g_example_error == 0`.

PASS: all four counters increase at coherent relative rates for at least 10 seconds and no example error is reported.

## Isolated DWT timing fixture

The timing firmware runs exactly one measurement case per image. Do not combine cases when collecting reference numbers.

### Composite ISR software point -> task continuation

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-dwt-timing.sh \
  --case event_to_task \
  --samples 10000
```

This build uses `HARDRT_TIMING_PROFILE=none`. It contains no kernel timing hooks. The interval starts at a DWT timestamp inside the TIM2 ISR and ends when the awakened latency task continues. It is a composite software ISR-to-task response metric, not hardware event-to-ISR latency.

PASS:

- debugger reaches `timing_target_reached`;
- `g_example_error == 0`;
- `g_timing_case_id == 1`;
- `g_timing_stats.count == g_timing_target_samples`;
- min/avg/max are finite and ordered (`min <= avg <= max`).

### Semaphore ISR call -> waiter READY

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-dwt-timing.sh \
  --case sem_isr_ready \
  --samples 10000
```

This build uses `HARDRT_TIMING_PROFILE=ipc` and enables only the IPC entry and waiter-READY measurement points needed by this metric. The end DWT timestamp is taken before statistics bookkeeping.

PASS:

- debugger reaches `timing_target_reached`;
- `g_example_error == 0`;
- `g_timing_case_id == 2`;
- `g_timing_stats.count == g_timing_target_samples`;
- min/avg/max are finite and ordered.

For either timing case, start OpenOCD in one terminal and run the repository GDB reader in another:

```bash
openocd -s /usr/share/openocd/scripts \
  -f scripts/openocd_h755.cfg \
  -c "init; reset halt"
```

```bash
gdb-multiarch -q \
  examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf \
  -batch -x scripts/gdb/timing.dbg
```

Record at minimum the HardRT commit SHA, timing case/profile, `SystemCoreClock`, sample count, timer PSC/ARR, toolchain version, board, date, and reported min/avg/max values.

## Fixed-priority hardware preemption

A dedicated Cortex-M preemption validation is required by issues #31 and #56 before v0.5.0 qualification. The test must show:

1. a lower-priority task already running;
2. an ISR making a higher-priority task READY;
3. PendSV dispatching the higher-priority task at the earliest safe exception-return point, without waiting for another kernel tick;
4. the higher-priority task blocking/completing;
5. the interrupted lower-priority task resuming from its saved execution point;
6. under `HRT_SCHED_PRIORITY_RR`, the lower-priority task retaining its unused quantum across that priority preemption.

This case is not considered manually qualified until the dedicated firmware/trace is added and executed. POSIX can test ordering rules but cannot substitute for this Cortex-M interrupt-preemption evidence.

## Manual qualification record

For a release candidate, record:

```text
HardRT SHA:
Toolchain:
Board / MCU revision:
Date:
Tester:
C blinky: PASS/FAIL
C++ blinky: PASS/FAIL
Counter demo: PASS/FAIL
DWT event_to_task: PASS/FAIL + result reference
DWT sem_isr_ready: PASS/FAIL + result reference
Priority preemption: PASS/FAIL + trace reference
Notes:
```
