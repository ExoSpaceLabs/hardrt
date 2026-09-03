# Hardware qualification policy

HardRT separates **development measurements** from **release qualification evidence**, but uses one human-facing STM32 runner for both functional validation and hardware benchmarking.

## Single STM32 runner

Use:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7
```

The mode rules are deliberately simple:

```text
(no --only)       = functional validation + all hardware benchmarks
--only functional = functional validation only
--only benchmark  = all hardware benchmarks only
```

The board/OpenOCD probe is a prerequisite and is always run. The default run is the complete release-candidate workflow. `--only` is a development filter, not a different test system.

Development evidence is written to a visible path:

```text
validation/stm32/<UTC>_<short-sha>/
```

Timestamped development runs are gitignored. There is no separate qualification wrapper and no promotion script.

For a release, run the default full mode from the exact release-candidate SHA, inspect the report, then manually copy or move the chosen run to:

```text
validation/stm32/releases/vX.Y.Z/
```

The repository should contain exactly one committed STM32 evidence package per released version.

## Functional hardware contracts

Functional validation contains nine behavior contracts, with the board probe reported separately:

1. C blinky task progress and qualitative relative LED rate;
2. C++ blinky task progress and qualitative relative LED rate;
3. scheduler counter demo;
4. fixed-priority ISR wake/preemption;
5. `PRIORITY_RR` retained-quantum preemption;
6. semaphore hardware contract;
7. queue hardware contract;
8. mutex hardware contract;
9. external-tick hardware contract.

Timing measurements are not counted as functional features. This keeps functional qualification stable when new benchmarks are added.

## Hardware benchmarks

The current suite contains **22 hardware benchmark images**.

### Latency and switch benchmarks

Four DWT measurements cover the current wake/switch path:

1. `event_to_task`;
2. `sem_isr_ready`;
3. `ready_to_task`;
4. `scheduler_decision`, including the PendSV switch decomposition.

Every benchmark is built and flashed as its own firmware image. `scripts/build-lib-stm32h7xx-dwt-timing.sh` selects the timing profile and private hook header required by that benchmark:

- `event_to_task`: direct application DWT timestamps, `HARDRT_TIMING_PROFILE=none`;
- `sem_isr_ready`: private `ipc` timing profile with ISR-entry/waiter-READY hooks;
- `ready_to_task`: private `ipc` timing profile with waiter-READY start hook;
- `scheduler_decision`: measurement-only PendSV image calling the unmodified production `hrt__schedule()`.

### Tick/sleeper scaling benchmarks

Eighteen measurements characterize tick/sleeper processing across application-task capacities **8, 16, and 32**. The same matrix is deliberately retained before and after sleeper-structure changes so it acts as an A/B performance contract rather than a one-off benchmark.

For each capacity it measures six workload shapes:

1. `none`: all worker task slots occupied by non-sleeping blocked tasks;
2. `one_sleep`: one long-duration sleeper, all other workers blocked;
3. `all_sleep`: all workers sleeping beyond the measurement window;
4. `one_expiry`: one worker expires every tick and immediately sleeps for one tick again;
5. `simultaneous`: all workers expire together every tick and then sleep again;
6. `staggered`: worker `i` sleeps for `i+1` ticks, distributing expiries across ticks.

The measured interval is application-side DWT around the production call:

```c
hrt_tick_from_isr();
```

These images use `HARDRT_TIMING_PROFILE=none`. No kernel timing hooks or replacement tick implementation are enabled.

#### Pre-delta baseline

Physical run `20260903T212538Z_4ab7709a` measured the former all-TCB sleeper scan at 64 MHz, 1 kHz and 10,000 samples per image. It completed **9/9 functional PASS and 22/22 benchmark PASS**.

Average tick-path cycles were:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 902 | 1050 | 1124 | 1367 | 2282 | 1700 |
| 16 | 1389 | 1506 | 1761 | 1808 | 3990 | 2435 |
| 32 | 1954 | 2597 | 3455 | 2952 | 8676 | 4413 |

At 32 application tasks, the old scan spent about 30.5 us per tick with no sleepers and 54.0 us with all workers sleeping but nothing expiring. That recurring no-expiry cost justified replacing the scan. The evidence archive SHA-256 is:

```text
6f32cf6ce1fb9db1955c65670bb759e3ee1cd7a7f5d83bbb383110173c2bd56d
```

#### Current sleeper structure

Current `develop` uses a static intrusive delta sleeper queue:

- bounded O(N) insertion when a task calls `hrt_sleep()`;
- O(1) tick decrement/check when nothing expires;
- O(K) wake processing when K tasks expire on the current tick;
- O(N) static metadata;
- no dynamic allocation;
- zero-delta followers for equal deadlines, preserving deterministic FIFO wake ordering;
- relative deltas, so sleeper ordering does not depend on sorting wrapped absolute tick values.

The task sleep transition publishes SLEEP state, delta-queue membership, and the reschedule request inside the port critical section. The public API and TCB layout are unchanged; `wake_tick` remains diagnostic state rather than the expiry-search mechanism.

Hosted coverage validates equal deadlines, staggered deadlines, repeated one-tick sleep/wake cycles, and multi-sleeper ordering across 32-bit tick wrap. Post-change **hardware performance is not claimed until the same 22-image benchmark matrix is run on the delta-queue implementation**.

TIM2 is run as a one-shot source for this matrix. After each measured tick, the ISR wakes a lower-priority benchmark driver **outside the measured interval**. Any worker tasks made READY by that tick have higher priority than the driver, so they run and return to their intended sleeping/blocked state before the driver can rearm the next timer shot. This prevents a later sample, especially the 32-task `simultaneous` case, from starting while the previous sample's workers are still recovering.

The three configured capacities are real library builds, not partially populated instances of one fixed-capacity build.

Instrumentation is therefore enabled only in benchmark images that need private hooks. Normal HardRT builds remain uninstrumented, and the tick-scaling benchmark itself requires no kernel instrumentation.

## Scheduler/PendSV benchmark

The scheduler benchmark measures the same ISR-wake path with a measurement-only PendSV handler and reports:

- outgoing context save;
- `hrt__schedule()` decision time;
- incoming context restore;
- complete PendSV software interval;
- PendSV entry to resumed blocked-API continuation;
- derived non-scheduler software and return/API intervals.

These are engineering measurements, not WCET proofs. The diagnostic handler itself adds probe work, so individual segments must not be treated as production instruction counts.

## Human observation

LED examples require only a qualitative visual check: both LEDs visibly toggle and their configured relative rates are distinguishable, for example one is roughly twice as fast. Exact 200 ms, 250 ms, 400 ms, or 500 ms timing is not a human acceptance criterion.

Automated counters and DWT measurements provide quantitative progress/timing evidence.

## What belongs on hardware

Hardware qualification focuses on behavior that can differ because of the Cortex-M port or real interrupt execution:

- PendSV/context switching and task progress;
- SysTick/external-tick integration;
- interrupt-safe synchronization and `BASEPRI` critical sections;
- scheduler-aware preemption and retained RR quantum;
- semaphore, queue, and mutex blocking/wake/handoff paths;
- C/C++ integration;
- latency, scheduler-switch, and bounded-work benchmarks;
- scaling of tick/sleeper work with configured task capacity and expiry population.

Pure argument validation and deterministic data-structure edge cases remain primarily hosted tests unless they interact with a port-specific path. Hardware validation is additive, not a replacement for the broader hosted suite.

## Release evidence requirements

A release STM32 package should be generated from the exact release-candidate SHA and record at minimum:

- clean tracked HardRT source;
- exact HardRT SHA;
- clean recorded STM32CubeH7 checkout and SHA;
- compiler/GDB/OpenOCD/CMake versions;
- board/core and clock information;
- board probe passing;
- all nine functional contracts passing;
- all current hardware benchmarks passing;
- timing sample counts and min/avg/max values;
- scheduler/PendSV decomposition;
- tick/sleeper scaling measurements for the qualified capacities;
- raw debugger/build logs.

A filtered `--only functional` or `--only benchmark` run is useful development evidence but is not the complete release package.
