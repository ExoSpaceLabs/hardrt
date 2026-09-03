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

The current benchmark suite contains four DWT measurements:

1. `event_to_task`;
2. `sem_isr_ready`;
3. `ready_to_task`;
4. `scheduler_decision`, including the PendSV switch decomposition.

Every benchmark is built and flashed as its own firmware image. `scripts/build-lib-stm32h7xx-dwt-timing.sh` selects the timing profile and private hook header required by that benchmark:

- `event_to_task`: direct application DWT timestamps, `HARDRT_TIMING_PROFILE=none`;
- `sem_isr_ready`: private `ipc` timing profile with ISR-entry/waiter-READY hooks;
- `ready_to_task`: private `ipc` timing profile with waiter-READY start hook;
- `scheduler_decision`: measurement-only PendSV image calling the unmodified production `hrt__schedule()`.

Instrumentation is therefore enabled only in benchmark builds that require it. Normal HardRT builds remain uninstrumented.

The benchmark list is expected to grow as qualification work adds measurements such as tick/sleeper scaling. Adding a benchmark must not change the functional-contract count.

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
- latency, scheduler-switch, and bounded-work benchmarks.

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
- raw debugger/build logs.

A filtered `--only functional` or `--only benchmark` run is useful development evidence but is not the complete release package.
