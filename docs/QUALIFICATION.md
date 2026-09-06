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

## Evidence handling

Development evidence is generated under:

```text
validation/stm32/<UTC>_<short-sha>/
```

A selected release package may be retained locally under:

```text
validation/stm32/releases/vX.Y.Z/
```

The `v` prefix above is only the local evidence-directory convention. Repository release tags use `X.Y.Z` without a `v` prefix.

These paths are intentionally gitignored. Generated hardware evidence must **not** be committed after qualification because that would change the SHA that was physically tested. The selected passing package is published as a GitHub Release artifact from the qualified `X.Y.Z` tag instead.

The source tree tagged for release must therefore be the same source tree that generated the passing report.

## Current v0.5 matrix

The consolidated v0.5 runner contains:

```text
13 functional contracts
38 hardware benchmark images
```

The board probe is reported separately.

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

The intrusive delta sleeper queue gives bounded O(N) insertion in task context, O(1) no-expiry tick work, and O(K) processing for K expiries plus READY publication cost.

### Event/notification timing benchmarks: 16

- `event_isr_to_task`;
- `notify_isr_to_task`;
- `event_scan_none` at 1, 8, 16, 32 waiters;
- `event_scan_one` at 1, 8, 16, 32 waiters;
- `event_scan_all` at 1, 8, 16, 32 waiters;
- `notify_isr_no_wake`;
- `notify_isr_wake`.

Signal timing uses application-side DWT timestamps with ordinary event/notification code uninstrumented. Event scan firmware validates expected cumulative wake count so timing cannot pass with an incomplete fan-out workload.

These measurements characterize bounded implementation behavior. They are not formal WCET proofs.

## Development evidence

Earlier H755 runs established the scheduler/lifecycle and event/notification functional baselines before the complete 16-image signal timing matrix was consolidated. They are development evidence, not substitutes for the exact-SHA release qualification run.

Representative historical wake/switch values remain documented in [STATISTICS.md](STATISTICS.md). Release-specific signal-profile numbers belong to the selected GitHub Release qualification artifact produced from the frozen SHA.

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

## v0.5.0 release evidence contract

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

After that run passes, do not modify the qualified source tree. Promote the same source commit through `develop` and `main`, tag `0.5.0` on `main`, and publish the qualification package and release binaries from that tag.

## Human observation

The two blinky cases retain a qualitative human check: both LEDs must visibly toggle and their configured relative rates must be distinguishable. Exact millisecond timing is not a human acceptance criterion.

## Relationship to broader hard-real-time qualification

Passing the v0.5 matrix demonstrates the tested H755 configuration satisfies the current behavioral contracts and provides measured timing evidence for the benchmark workloads. It does not establish universal WCET bounds. Analytical critical-section bounds, bounded mutex priority inversion, queue-copy scaling, richer interference analysis, and complete machine-readable timing evidence remain 1.0-quality work.
