# Hardware qualification policy

HardRT separates **development measurements** from **release qualification evidence**, but uses one human-facing STM32 runner for both.

## Single STM32 runner

Use:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7
```

The mode rules are deliberately simple:

```text
(no --only)       = functional matrix + scheduler diagnostics
--only functional = functional matrix only
--only scheduler  = scheduler/PendSV diagnostics only
```

The default run is the complete release-candidate workflow. `--only` is a development filter, not a different test system.

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

## Functional hardware matrix

The functional matrix contains 13 cases:

1. board/OpenOCD probe;
2. C blinky task progress and qualitative relative LED rate;
3. C++ blinky task progress and qualitative relative LED rate;
4. scheduler counter demo;
5. DWT `event_to_task` timing fixture;
6. DWT `sem_isr_ready` timing fixture;
7. DWT `ready_to_task` timing fixture;
8. fixed-priority ISR wake/preemption;
9. `PRIORITY_RR` retained-quantum preemption;
10. semaphore hardware contract;
11. queue hardware contract;
12. mutex hardware contract;
13. external-tick hardware contract.

Scheduler/PendSV timing diagnostics are reported separately from the 13 functional contracts. They do not turn the matrix into an artificial fourteenth functional feature.

## Scheduler diagnostics

The scheduler diagnostic measures the same ISR-wake path with a measurement-only PendSV handler and reports:

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
- latency and scheduler-switch measurements.

Pure argument validation and deterministic data-structure edge cases remain primarily hosted tests unless they interact with a port-specific path. Hardware validation is additive, not a replacement for the broader hosted suite.

## Release evidence requirements

A release STM32 package should be generated from the exact release-candidate SHA and record at minimum:

- clean tracked HardRT source;
- exact HardRT SHA;
- clean recorded STM32CubeH7 checkout and SHA;
- compiler/GDB/OpenOCD/CMake versions;
- board/core and clock information;
- all 13 functional cases passing;
- scheduler/PendSV diagnostic output;
- timing sample counts and min/avg/max values;
- raw debugger/build logs.

A filtered `--only functional` or `--only scheduler` run is useful development evidence but is not the complete release package.
