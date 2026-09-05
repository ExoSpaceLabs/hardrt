# STM32H755 Manual Validation

HardRT's hosted Linux/POSIX tests are automatic. STM32H755 runtime validation remains manual until a hardware CI runner exists. Cross-compilation proves that firmware builds and links; it does not prove interrupt, PendSV, GPIO, clock, synchronization, or board behavior.

The supported manual target is currently NUCLEO-H755ZI-Q, exercising CM7 while CM4 is held in reset.

## Functional and baseline timing runner

Run from the repository root:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7
```

The runner owns build, flash, OpenOCD/GDB sessions, result parsing, evidence capture, and final console/report summaries.

### Modes

```text
(no --only)       = functional + benchmark
--only functional = functional only
--only benchmark  = benchmark only
```

The board probe always runs first and is reported separately. The default unfiltered run is the release-candidate path for the established functional and scheduler/tick timing suite; filtered modes are development shortcuts.

For release-style evidence use:

```bash
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --clean-builds
```

## v0.5 event / notification profiling runner

The event-flags and task-notification feature has a separate timing runner so the historical 22-image scheduler/tick benchmark set remains directly comparable across releases:

```bash
./scripts/stm32_signal_profile.sh /path/to/STM32CubeH7
```

It executes **16 additional timing images**:

- `event_isr_to_task`;
- `notify_isr_to_task`;
- `event_scan_none`, `event_scan_one`, and `event_scan_all`, each with 1, 8, 16, and 32 actual registered event waiters;
- `notify_isr_no_wake`;
- `notify_isr_wake`.

The signal images use direct DWT timestamps with `HARDRT_TIMING_PROFILE=none`. Event scan results also validate the expected cumulative wake count so the measured path is tied to the requested fan-out.

For a v0.5.0 release candidate, complete release evidence consists of a passing full functional/baseline-timing run **and** a passing signal-profiling run from the same frozen SHA.

## Evidence location

Development runs are written under:

```text
validation/stm32/<UTC>_<short-sha>/
validation/stm32/<UTC>_<short-sha>_signals/
```

Timestamped development directories are gitignored.

For a release candidate, run from the exact final SHA and manually retain the selected passing packages under:

```text
validation/stm32/releases/vX.Y.Z/
```

## Common requirements

- ARM GNU bare-metal toolchain (`arm-none-eabi-*`)
- OpenOCD with ST-Link support
- `gdb-multiarch` or `arm-none-eabi-gdb`
- local STM32CubeH7 checkout supplied to the runner
- physical NUCLEO-H755ZI-Q connected through ST-Link

For release evidence, use clean tracked HardRT source and a clean recorded STM32CubeH7 checkout.

## Functional validation: 13 contracts

The board probe is a prerequisite, not a functional feature. Functional mode runs **13 behavior contracts**:

1. **C blinky**: task counters advance, no example error, both LEDs visibly toggle with distinguishable relative rates.
2. **C++ blinky**: same contract through the C++ API.
3. **Scheduler counter demo**: task-entry/post-sleep counters advance and no example error is recorded.
4. **Fixed-priority hardware preemption**: real TIM2 ISR wakes a blocked higher-priority task before interrupted lower-priority Thread mode continues.
5. **Global RR mixed-priority contract**: one global FIFO ignores task priority; ISR wake reports no immediate priority-based steal and the woken task joins the global tail.
6. **`PRIORITY_RR` retained-quantum preemption**: higher-priority preemption preserves interrupted-task queue precedence and unused quantum.
7. **Semaphore hardware contract**: counting/saturation behavior plus real ISR wake and scheduler-aware `need_switch`.
8. **Queue hardware contract**: FIFO/full/empty plus ISR send/receive wake paths, payload preservation and priority handoff.
9. **Mutex hardware contract**: ownership, blocking, direct handoff and immediate execution of a newly eligible higher-priority owner.
10. **Event flags hardware contract**: wait-all is completed incrementally by task and real TIM2 ISR producers; clear-on-exit removes matched bits; wait-any retained bits remain set until explicitly cleared; ISR wake reports scheduler-aware `need_switch` and preempts when required.
11. **Task notification hardware contract**: pending data survives unrelated semaphore blocking; overwrite/no-overwrite/set-bits semantics are checked; a real TIM2 ISR notification wakes and preempts correctly; increment plus counting-take preserves and consumes the count correctly.
12. **External tick hardware contract**: SysTick disabled, periodic TIM2 drives `hrt_tick_from_isr()`, sleep duration/tick accounting are correct and awakened higher-priority work preempts.
13. **BASEPRI critical-section contract**: unmasked/weaker/stricter/nested entry cases preserve the HardRT ceiling and exact pre-entry mask state.

Timing measurements are deliberately not counted as functional contracts.

### External tick startup rule

The application owns an external tick source. It must not begin routing periodic interrupts into HardRT before scheduler execution has started.

The H755 validator therefore configures TIM2 before scheduler start but enables the timer from the first dispatched application task. This prevents an external tick from observing the pre-dispatch `g_current == -1` state.

## Established hardware benchmark suite: 22 images

Baseline benchmark mode runs four latency/switch images plus an 18-point tick/sleeper scaling matrix.

### Latency and switch benchmarks

1. `event_to_task`
2. `sem_isr_ready`
3. `ready_to_task`
4. `scheduler_decision` / PendSV decomposition

`event_to_task` is a legacy semaphore-backed composite ISR-to-task measurement. Its name predates the v0.5 event-flags API and it is retained unchanged for historical timing comparisons.

Each benchmark is a separate build/flash image. Timing instrumentation is compile-time selected and normal HardRT builds remain uninstrumented.

The scheduler diagnostic reports:

```text
pendsv_save
scheduler_decision
pendsv_restore
pendsv_software
pendsv_to_task
derived_software_other_avg
derived_return_and_api_avg
```

These values are engineering measurements, not WCET proofs.

### Tick/sleeper scaling matrix

HardRT is rebuilt at application-task capacities:

```text
8, 16, 32
```

For each capacity the runner measures:

| Scenario | Worker state / expiry pattern |
|---|---|
| `none` | all worker slots occupied by non-sleeping blocked tasks |
| `one_sleep` | one long sleeper, remaining workers blocked |
| `all_sleep` | all workers sleeping beyond the sample window |
| `one_expiry` | one worker expires every tick |
| `simultaneous` | all workers expire together every tick |
| `staggered` | worker `i` sleeps `i+1` ticks |

This produces 18 measurements around the production call:

```c
hrt_tick_from_isr();
```

Current `develop` uses a static intrusive delta sleeper queue with bounded O(N) task-context insertion, O(1) no-expiry tick work and O(K) work for K expiries.

## Accepted event / notification functional development baseline

The first complete 13-contract hardware run for the event/notification branch was accepted on:

```text
Run ID:       20260905T152422Z_6f4ef62a
HardRT SHA:   6f4ef62a8a0d13a0632537c6e65a50cbd315d656
Tracked tree: clean
STM32CubeH7:  f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
Samples:      10000 per benchmark image
```

Result:

```text
Board probe:           PASS
Functional contracts:  13 / 13 PASS
Hardware benchmarks:   22 / 22 PASS
Overall:                PASS
```

The event-flags and task-notification hardware contracts both passed. This establishes physical functional behavior for the new primitives at that development SHA. It does **not** include the later dedicated signal timing matrix, which must be run on the profiling-enabled head.

Latency/switch values from that run:

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` | 1427 | 1470 | 2024 |
| `sem_isr_ready` | 355 | 355 | 356 |
| `ready_to_task` | 948 | 996 | 1590 |
| `scheduler_decision` | 356 | 363 | 394 |

Scheduler/PendSV average decomposition from that run:

```text
pendsv_save=99 cycles
scheduler_decision=363 cycles
pendsv_restore=70 cycles
pendsv_software=579 cycles
pendsv_to_task=797 cycles
```

Tick/sleeper averages from that run:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 561 | 598 | 580 | 858 | 1932 | 1194 |
| 16 | 561 | 598 | 580 | 858 | 3236 | 1315 |
| 32 | 545 | 566 | 585 | 865 | 6155 | 1478 |

The no-expiry cases remain effectively independent of configured task capacity. Actual expiry work remains proportional to the number of workers that wake.

This is accepted **development evidence**, not final v0.5.0 release evidence. The final release qualification must be performed on the frozen release-candidate SHA and include the 16-image signal profiling matrix.

## Human LED acceptance

The LED check is qualitative. PASS means both LEDs visibly toggle and their relative configured rates are distinguishable. Automated counters prove task progress; DWT benchmarks provide quantitative timing evidence.

## Result summary

The established runner records:

- HardRT SHA and tracked source state
- STM32CubeH7 SHA/state
- board probe result
- functional `N/13 PASS`, failure and not-run counts
- baseline benchmark `N/22 PASS`, failure and not-run counts
- timing min/avg/max
- scheduler/PendSV breakdown
- tick/sleeper scenario/capacity/wake metadata
- RR trace/quantum evidence
- raw build/OpenOCD/GDB log locations

The signal profiling runner records the same source identity plus all 16 event/notification timing cases, configured event waiter count, expected wake fan-out, observed wake count, and cycle min/avg/max.

See [QUALIFICATION.md](QUALIFICATION.md) for the release-evidence policy, [STATISTICS.md](STATISTICS.md) for timing interpretation, and [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md) for the v0.5 signal semantics and profiling contract.
