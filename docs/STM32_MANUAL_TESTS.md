# STM32H755 Manual Validation

HardRT's hosted Linux/POSIX tests are automatic. STM32H755 runtime validation is intentionally manual until a hardware CI runner exists. Cross-compilation proves that firmware links; it does not prove interrupt, PendSV, GPIO, clock, or board behavior.

Run these tests from the repository root on a NUCLEO-H755ZI-Q. The examples hold CM4 in reset and exercise CM7.

## Common requirements

- ARM GNU bare-metal toolchain (`arm-none-eabi-*`).
- OpenOCD with ST-Link support.
- A local STM32CubeH7 checkout. Set `STM32CUBE_H7_ROOT` when the default path is not valid.
- A clean working tree built from the exact HardRT commit being qualified.

Normal STM32 examples expose `g_example_error` for debugger inspection. A successful running example keeps it at `0`.

Common failure codes used by the normal examples:

| Code | Meaning |
|---:|---|
| 0 | no detected example-level error |
| 1 | `hrt_init()` failed |
| 2 | first task creation failed |
| 3 | second task creation failed |
| 4 | `hrt_start()` unexpectedly returned |

A failure path executes `BKPT` and then remains in `WFI`, making the failure deterministic under a debugger.

Hosted CI cross-builds every firmware listed here. Hardware PASS/FAIL remains a manual release-qualification step.

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

## Fixed-priority hardware preemption

`examples/hardrt_h755_preemption` is a validation fixture, not a demonstration application. A TIM2 hardware interrupt wakes a blocked highest-priority task while a lower-priority task is executing a busy loop with no HardRT scheduling calls. This is the Cortex-M evidence that POSIX cannot provide.

The firmware exports:

- `g_validation_pass`, `g_example_error`, `g_validation_case`;
- `g_irq_count`, `g_need_switch`, `g_high_runs`;
- `g_low_a_counter`, `g_low_b_counter`;
- `g_a_start_tick`, `g_irq_tick`, `g_high_tick`, `g_a_resume_tick`, `g_b_first_tick`;
- `g_expected_remaining_ticks`, `g_observed_remaining_ticks`;
- `g_sequence[5]`.

Build and flash one case at a time.

### Strict `HRT_SCHED_PRIORITY`

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-preemption.sh --case priority
```

Then:

```bash
openocd -s /usr/share/openocd/scripts \
  -f scripts/openocd_h755.cfg \
  -c "init; reset halt"
```

```bash
gdb-multiarch -q \
  examples/hardrt_h755_preemption/build-cortex_m/hardrt_h755_preemption.elf \
  -batch -x scripts/gdb/preemption.dbg
```

Required behavior:

1. high-priority task starts first and blocks on a semaphore;
2. low-A starts and remains CPU-bound;
3. TIM2 ISR wakes high and `need_switch` is asserted;
4. PendSV runs high before Thread mode resumes low-A;
5. high blocks again;
6. the interrupted low-A resumes from its saved execution context.

PASS requires:

- `g_validation_pass == 1`;
- `g_example_error == 0`;
- `g_irq_count == 1`;
- `g_need_switch == 1`;
- `g_high_runs == 1`;
- sequence slots begin `[1, 2, 3, 4]` = low-A, IRQ, high, resumed low-A.

Error `20` specifically means low-A executed after the IRQ but before high ran, so fixed-priority preemption was not immediate at the earliest safe exception-return point.

### `HRT_SCHED_PRIORITY_RR` retained quantum

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-preemption.sh --case priority_rr
```

Run the same OpenOCD/GDB procedure above.

This case adds low-B at the same priority as low-A with a 20-tick RR quantum. TIM2 fires a few ticks into A's first quantum. Required sequence:

```text
low-A -> IRQ -> high -> low-A -> low-B
```

The high-priority interruption must not itself rotate A behind B. After high blocks, A must resume with its unused quantum. B may run only when A's retained quantum expires or A explicitly yields.

The firmware derives the remaining quantum from the actual observed tick at which the IRQ arrived:

```text
expected_remaining = 20 - (irq_tick - a_start_tick)
observed_remaining = b_first_tick - a_resume_tick
```

One tick of boundary tolerance is allowed.

PASS requires:

- `g_validation_pass == 1` and `g_example_error == 0`;
- sequence `[1, 2, 3, 4, 5]`;
- A resumes before B;
- observed remaining quantum matches expected remaining quantum within one tick.

This contract has been physically validated on NUCLEO-H755ZI-Q. Keep this case in every hardware qualification run because scheduler data-structure or PendSV changes can regress queue precedence even when hosted ordering tests remain green.

Preemption validator error codes:

| Code | Meaning |
|---:|---|
| 0 | validation passed / no error |
| 1 | `hrt_init()` failed |
| 2 | high task creation failed |
| 3 | low-A creation failed |
| 4 | low-B creation failed |
| 5 | `hrt_start()` unexpectedly returned |
| 10 | high semaphore take failed |
| 11 | high task unexpectedly resumed after its long sleep |
| 20 | low-A ran after IRQ before high task ran |
| 21 | unexpected IRQ count |
| 22 | ISR wake did not report `need_switch == 1` |
| 23 | unexpected high-task run count |
| 24 | IRQ occurred outside A's first RR quantum; fixture timing invalid |
| 30 | low-B ran before the preemption IRQ; fixture timing invalid |
| 31 | low-B ran after IRQ but before high task |
| 32 | low-B ran before interrupted low-A resumed; priority-preemption queue-order failure |
| 33 | low-A resumed, but its observed remaining quantum did not match the retained quantum |

## Isolated DWT timing fixture

The timing firmware runs exactly one measurement case per image. Do not combine cases inside one firmware image when collecting reference numbers. The full qualification runner builds and flashes the images sequentially.

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
- result `case_id == 1`;
- result count equals the configured sample target;
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
- result `case_id == 2`;
- result count equals the configured sample target;
- min/avg/max are finite and ordered.

### Waiter READY -> resumed task continuation

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-dwt-timing.sh \
  --case ready_to_task \
  --samples 10000
```

This build also uses `HARDRT_TIMING_PROFILE=ipc`, but with a separate hook definition that takes the start timestamp exactly after the semaphore waiter is marked READY. The end timestamp is taken in the latency task immediately after the blocked `hrt_sem_take()` returns.

The interval therefore includes the remainder of the ISR-facing semaphore path, critical-section exit, PendSV/scheduler selection, task context restore, exception return, and blocked API return. It is the hardware dispatch-response metric used to isolate the scheduler/context part of the composite `event_to_task` number. It is **not** a pure context-switch microbenchmark.

PASS:

- debugger reaches `timing_target_reached`;
- `g_example_error == 0`;
- result `case_id == 3`;
- result count equals the configured sample target;
- min/avg/max are finite and ordered.

For any timing case, start OpenOCD in one terminal and run the repository GDB reader in another:

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

## Full qualification runner

The preferred hardware workflow runs the complete matrix and writes a timestamped evidence directory:

```bash
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --clean-builds
```

The current runner executes nine cases: board probe, C blinky, C++ blinky, scheduler counter demo, the three isolated DWT timing images, fixed-priority hardware preemption, and `PRIORITY_RR` retained-quantum preemption.

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
Priority preemption: PASS/FAIL + trace reference
Priority+RR preemption/retained quantum: PASS/FAIL + trace reference
DWT event_to_task: PASS/FAIL + result reference
DWT sem_isr_ready: PASS/FAIL + result reference
DWT ready_to_task: PASS/FAIL + result reference
Notes:
```