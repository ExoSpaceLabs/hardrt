# Hardware qualification policy

HardRT separates **development measurements** from **release qualification evidence**, while using one human-facing STM32 runner for both functional validation and hardware benchmarking.

## Single STM32 runner

Use:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7
```

Mode rules:

```text
(no --only)       = functional validation + all hardware benchmarks
--only functional = functional validation only
--only benchmark  = all hardware benchmarks only
```

The board/OpenOCD probe is a prerequisite and is always run. The default run is the complete release-candidate workflow. `--only` is a development filter, not a different test system.

Development evidence is written to:

```text
validation/stm32/<UTC>_<short-sha>/
```

Timestamped development runs are gitignored. For a release, run the default full mode from the exact release-candidate SHA, inspect the report, then manually copy or move the selected run to:

```text
validation/stm32/releases/vX.Y.Z/
```

The repository should contain exactly one committed STM32 evidence package per released version.

## Current accepted v0.5 development baseline

The scheduler/lifecycle hardening phase is accepted on physical NUCLEO-H755ZI-Q hardware at:

```text
Run ID:       20260905T134123Z_80f2042f
HardRT SHA:   80f2042f2c64053a9ea888666474c5dad5f72797
Branch:       develop
Tracked tree: clean
STM32CubeH7:  f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
Core:         CM7
ARM GCC:      10.3.1
GDB:          12.1
OpenOCD:      0.11.0
Samples:      10000 per benchmark image
```

Result:

```text
Board probe:           PASS
Functional contracts:  11 / 11 PASS
Hardware benchmarks:   22 / 22 PASS
Overall:                PASS
```

This is accepted **development qualification**, not the final v0.5.0 release package. Any later release-facing code change moves the candidate SHA and requires the final unfiltered run to be repeated.

## Functional hardware contracts

Functional validation contains **11 behavior contracts**, with the board probe reported separately:

1. C blinky task progress and qualitative relative LED rate;
2. C++ blinky task progress and qualitative relative LED rate;
3. scheduler counter/lifecycle progress;
4. fixed-priority ISR wake/preemption;
5. global-RR mixed-priority FIFO and ISR-wake behavior;
6. `PRIORITY_RR` retained-quantum preemption;
7. semaphore hardware contract;
8. queue hardware contract;
9. mutex hardware contract;
10. external TIM2 tick contract using `hrt_tick_from_isr()`;
11. Cortex-M `BASEPRI` critical-section preservation.

Timing measurements are not counted as functional features. This keeps functional qualification stable when benchmarks are added or reorganized.

The external tick source is application-owned. It must not begin routing interrupts into HardRT before scheduler execution has started. The H755 fixture starts TIM2 from the first dispatched application task, which prevents an external tick from observing the pre-dispatch `g_current == -1` startup state.

## Hardware benchmarks

The current suite contains **22 hardware benchmark images**.

### Latency and switch benchmarks

Four DWT measurements cover the current wake/switch path:

1. `event_to_task`;
2. `sem_isr_ready`;
3. `ready_to_task`;
4. `scheduler_decision`, including the PendSV switch decomposition.

Every benchmark is built and flashed as its own firmware image. Timing profiles are compile-time selected and ordinary HardRT builds remain uninstrumented.

Current accepted values at `80f2042f`, 64 MHz, 10,000 samples:

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` | 1407 | 1457 | 2001 |
| `sem_isr_ready` | 300 | 319 | 327 |
| `ready_to_task` | 922 | 1001 | 1597 |
| `scheduler_decision` | 365 | 380 | 411 |

Scheduler/PendSV decomposition:

| Segment | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| PendSV save | 88 | 88 | 88 |
| scheduler decision | 365 | 380 | 411 |
| PendSV restore | 63 | 63 | 63 |
| PendSV software | 562 | 577 | 608 |
| PendSV to task continuation | 779 | 800 | 1365 |

These are engineering measurements, not WCET proofs. The diagnostic handler adds measurement work and derived intervals must not be presented as exact production instruction counts.

### Tick/sleeper scaling benchmarks

Eighteen measurements characterize production `hrt_tick_from_isr()` work across application-task capacities **8, 16, and 32**, with six workload shapes per capacity:

1. `none`;
2. `one_sleep`;
3. `all_sleep`;
4. `one_expiry`;
5. `simultaneous`;
6. `staggered`.

Current `develop` uses a static intrusive delta sleeper queue:

```text
hrt_sleep() insertion: bounded O(N) in task context
no-expiry tick:        O(1)
K expiries:            O(K)
metadata:               O(N), static
allocation:             none
```

Average cycle counts from the accepted `80f2042f` run:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 543 | 544 | 592 | 830 | 1893 | 1171 |
| 16 | 543 | 544 | 592 | 830 | 3181 | 1290 |
| 32 | 504 | 552 | 555 | 837 | 5249 | 1331 |

The no-expiry cases no longer scale materially with configured task capacity. Actual expiry work remains proportional to the number of tasks that genuinely wake, which is the intended structural contract.

## Human observation

LED examples require only a qualitative visual check: both LEDs visibly toggle and their configured relative rates are distinguishable. Exact millisecond timing is not a human acceptance criterion.

Automated counters and DWT measurements provide quantitative progress/timing evidence.

## What belongs on hardware

Hardware qualification focuses on behavior that can differ because of the Cortex-M port or real interrupt execution:

- PendSV/context switching and task progress;
- SysTick/external-tick integration;
- interrupt-safe synchronization and `BASEPRI` critical sections;
- scheduler-aware preemption and retained RR quantum;
- semaphore, queue, and mutex blocking/wake/handoff paths;
- C/C++ integration;
- scheduler and bounded tick/sleeper work;
- scaling with configured task capacity and expiry population.

Pure argument validation and deterministic data-structure edge cases remain primarily hosted tests unless they interact with a port-specific path. Hardware validation is additive, not a replacement for the broader hosted suite.

## Release evidence requirements

A release STM32 package must be generated from the exact release-candidate SHA and record at minimum:

- clean tracked HardRT source;
- exact HardRT SHA;
- clean recorded STM32CubeH7 checkout and SHA;
- compiler/GDB/OpenOCD/CMake versions;
- board/core and clock information;
- board probe passing;
- all **11** functional contracts passing;
- all **22** current hardware benchmarks passing;
- timing sample counts and min/avg/max values;
- scheduler/PendSV decomposition;
- tick/sleeper scaling measurements for the qualified capacities;
- raw debugger/build logs.

A filtered `--only functional` or `--only benchmark` run is useful development evidence but is not the complete release package.

## Relationship to broader hard-real-time qualification

Passing this suite demonstrates that the tested Cortex-M configuration satisfies the current scheduler, lifecycle, context, IPC, tick and critical-section behavioral contracts and that the benchmark workloads remain within their measured ranges.

It does **not** by itself prove a universal WCET bound. Broader work such as priority-inversion mitigation, maximum critical-section characterization, queue-copy scaling, interference analysis, machine-readable timing output and complete platform metadata remains tracked for post-v0.5 hard-real-time qualification.
