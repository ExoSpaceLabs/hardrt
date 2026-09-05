# Hardware qualification policy

HardRT separates development measurements from release qualification evidence while using one human-facing STM32 runner for both functional validation and hardware benchmarking.

## Single STM32 runner

Use:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

Modes:

```text
(no --only)       = every functional contract + every benchmark
--only functional = functional contracts only
--only benchmark  = benchmarks only
```

The board/OpenOCD probe always runs first. Only the default unfiltered mode is a complete release-candidate hardware run.

Development evidence is written to:

```text
validation/stm32/<UTC>_<short-sha>/
```

Timestamped development runs are gitignored. A selected release package is retained under:

```text
validation/stm32/releases/vX.Y.Z/
```

The repository should contain exactly one selected STM32 evidence package per release.

## Current v0.5 matrix

The consolidated v0.5 runner contains:

```text
13 functional contracts
38 hardware benchmark images
```

The board probe is a prerequisite and is reported separately.

### Functional contracts: 13

1. C blinky task progress/relative LED rate;
2. C++ blinky task progress/relative LED rate;
3. scheduler counter/lifecycle progress;
4. fixed-priority TIM2 ISR wake/preemption;
5. global-RR mixed-priority FIFO/ISR-wake behavior;
6. `PRIORITY_RR` retained-quantum preemption;
7. semaphore hardware contract;
8. queue hardware contract;
9. mutex hardware contract;
10. event-flags hardware contract;
11. task-notification hardware contract;
12. external TIM2 tick contract using `hrt_tick_from_isr()`;
13. Cortex-M BASEPRI critical-section preservation.

Timing measurements are not counted as functional features.

### Historical scheduler/tick benchmarks: 22

Four latency/switch images:

1. `event_to_task`;
2. `sem_isr_ready`;
3. `ready_to_task`;
4. `scheduler_decision` with PendSV decomposition.

`event_to_task` is a legacy semaphore-backed composite name retained for historical comparison. It is **not** the v0.5 event-flags primitive.

Eighteen tick/sleeper images exercise capacities 8/16/32 across:

- `none`;
- `one_sleep`;
- `all_sleep`;
- `one_expiry`;
- `simultaneous`;
- `staggered`.

The intrusive delta sleeper queue gives bounded O(N) insertion in task context, O(1) no-expiry tick work, and O(K) processing for K expiries, plus READY publication cost.

### Event/notification timing benchmarks: 16

- `event_isr_to_task`;
- `notify_isr_to_task`;
- `event_scan_none` at 1, 8, 16, 32 waiters;
- `event_scan_one` at 1, 8, 16, 32 waiters;
- `event_scan_all` at 1, 8, 16, 32 waiters;
- `notify_isr_no_wake`;
- `notify_isr_wake`.

Signal timing uses application-side DWT timestamps with ordinary event/notification code uninstrumented. Event scan firmware also validates expected cumulative wake count so timing cannot pass with an incomplete fan-out workload.

These measurements characterize bounded implementation behavior. They are not formal WCET proofs.

## Development evidence

The first complete event/notification functional H755 run was:

```text
Run ID:       20260905T152422Z_6f4ef62a
HardRT SHA:   6f4ef62a8a0d13a0632537c6e65a50cbd315d656
Tracked tree: clean
STM32CubeH7:  f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
Board/core:   NUCLEO-H755ZI-Q / CM7
ARM GCC:      10.3.1
Samples:      10000 per benchmark image
```

Result:

```text
Board probe:           PASS
Functional contracts:  13 / 13 PASS
Historical benchmarks: 22 / 22 PASS
Overall:                PASS
```

The new event and notification hardware contracts passed. This run predates the later consolidated 16-image signal timing matrix and is therefore development evidence rather than final release evidence.

Representative latency values from that development run:

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` (legacy semaphore composite) | 1427 | 1470 | 2024 |
| `sem_isr_ready` | 355 | 355 | 356 |
| `ready_to_task` | 948 | 996 | 1590 |
| `scheduler_decision` | 356 | 363 | 394 |

At the recorded 64 MHz clock these are configuration-specific measurements only.

## What belongs on hardware

Physical qualification focuses on paths affected by real Cortex-M execution:

- PendSV/context switching and task progress;
- SysTick/external-tick integration;
- BASEPRI critical-section behavior;
- scheduler-aware task/ISR wake and retained RR quantum;
- semaphore, queue, mutex, event, and notification block/wake/handoff paths;
- event and notification ISR timing;
- event waiter-scan/fan-out scaling;
- scheduler/tick/sleeper timing and capacity scaling;
- C/C++ target integration.

Pure argument/data-structure edge cases remain primarily hosted tests unless they interact with a target-specific path.

## Final v0.5.0 release evidence

Before the hardware run, all release-facing source/docs/version changes must be complete and hosted CI must pass on one frozen SHA.

The final STM32 package must then be generated from that exact SHA and record at minimum:

- clean tracked HardRT source and exact SHA;
- clean/pinned STM32CubeH7 checkout and SHA;
- compiler/GDB/OpenOCD/CMake versions;
- board/core identity;
- board probe PASS;
- **13/13 functional PASS**;
- **38/38 benchmark PASS**;
- benchmark sample counts and min/avg/max cycles;
- event waiter count and expected/observed wake fan-out for scan cases;
- scheduler/PendSV decomposition;
- tick/sleeper scaling metadata;
- raw build/OpenOCD/GDB logs.

After that run passes, do not modify the qualified source tree. Promote the same commit/tree through `develop` and `main`, tag `v0.5.0` on `main`, and publish release artifacts from the tag.

## Human observation

The two blinky cases retain a qualitative human check: both LEDs must visibly toggle and their configured relative rates must be distinguishable. Exact millisecond timing is not a human acceptance criterion.

## Relationship to broader hard-real-time qualification

Passing the v0.5 matrix demonstrates the tested H755 configuration satisfies the current behavioral contracts and provides measured timing evidence for the benchmark workloads. It does not by itself establish universal WCET bounds. Analytical critical-section bounds, bounded mutex priority inversion, queue-copy scaling, richer interference analysis, and complete machine-readable timing evidence remain 1.0-quality work.
