# STM32H755 Manual Validation

HardRT's hosted Linux/POSIX tests are automatic. STM32H755 runtime validation remains manual until a hardware CI runner exists. Cross-compilation proves firmware builds and links; it does not prove interrupt, PendSV, GPIO, clock, synchronization, or board behavior.

The supported manual target is NUCLEO-H755ZI-Q, exercising CM7 while CM4 is held in reset.

## One human-facing runner

Run from the repository root:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

This is the only human-facing hardware qualification command. It owns build, flash, OpenOCD/GDB sessions, functional validation, all timing/profile images, result parsing, evidence capture, and final console/report summaries. Event/notification profiling is part of this runner rather than a second procedure.

### Modes

```text
(no --only)       = functional + benchmark
--only functional = functional only
--only benchmark  = benchmark only
```

The board probe always runs first. Only the default unfiltered mode is release-candidate evidence; filtered modes are development shortcuts.

## Evidence location and release handling

Development runs are written under:

```text
validation/stm32/<UTC>_<short-sha>/
```

For a release candidate, retain the selected passing package locally under:

```text
validation/stm32/releases/vX.Y.Z/
```

Both development and release-evidence directories are gitignored deliberately. **Do not commit generated qualification evidence after the board run**, because doing so would change the SHA that was physically qualified.

The selected package is instead published as a GitHub Release artifact from the qualified `vX.Y.Z` tag. The source commit tagged for release must be the same source tree that produced the passing hardware report.

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
10. **Event flags hardware contract**: wait-all is completed incrementally by task and real TIM2 ISR producers; clear-on-exit removes matched bits; retained wait-any bits remain until explicitly cleared; ISR wake reports scheduler-aware `need_switch` and preempts when required.
11. **Task notification hardware contract**: pending data survives unrelated semaphore blocking; overwrite/no-overwrite/set-bits semantics are checked; a real TIM2 ISR notification wakes and preempts correctly; increment plus counting-take preserves and consumes the count correctly.
12. **External tick hardware contract**: SysTick disabled, periodic TIM2 drives `hrt_tick_from_isr()`, sleep/tick accounting is correct, and awakened higher-priority work preempts.
13. **BASEPRI critical-section contract**: unmasked/weaker/stricter/nested entry cases preserve the HardRT ceiling and exact pre-entry mask state.

Timing measurements are deliberately not counted as functional contracts.

### External tick startup rule

The application owns an external tick source. It must not begin routing periodic interrupts into HardRT before scheduler execution has started. The H755 validator configures TIM2 before scheduler start but enables the timer from the first dispatched application task.

## Hardware benchmark suite: 38 images

Benchmark mode runs **38 separate build/flash images**:

- 4 established scheduler/semaphore latency and switch measurements;
- 16 v0.5 event/notification signal measurements;
- 18 tick/sleeper scaling measurements.

Every image is rebuilt and flashed independently. Ordinary HardRT builds remain uninstrumented.

### Established latency and switch benchmarks

1. `event_to_task`
2. `sem_isr_ready`
3. `ready_to_task`
4. `scheduler_decision` / PendSV decomposition

`event_to_task` is the historical semaphore-backed composite ISR-to-task benchmark. Its name predates the v0.5 event-flags API and is retained unchanged for historical comparison.

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

These are engineering measurements, not WCET proofs.

### v0.5 event/notification profiling: 16 images

- `event_isr_to_task`
- `notify_isr_to_task`
- `event_scan_none` with 1, 8, 16 and 32 registered waiters
- `event_scan_one` with 1, 8, 16 and 32 registered waiters
- `event_scan_all` with 1, 8, 16 and 32 registered waiters
- `notify_isr_no_wake`
- `notify_isr_wake`

These cases use direct DWT timestamps with `HARDRT_TIMING_PROFILE=none`; the production event/notification implementation is not instrumented internally.

The event scan cases validate actual waiter load and expected wake fan-out in addition to timing. The 32-waiter images rebuild HardRT with enough application slots for 32 real waiters plus the controller task.

### Tick/sleeper scaling matrix: 18 images

HardRT is rebuilt at application-task capacities 8, 16 and 32. Each capacity measures:

| Scenario | Worker state / expiry pattern |
|---|---|
| `none` | all worker slots occupied by non-sleeping blocked tasks |
| `one_sleep` | one long sleeper, remaining workers blocked |
| `all_sleep` | all workers sleeping beyond the sample window |
| `one_expiry` | one worker expires every tick |
| `simultaneous` | all workers expire together every tick |
| `staggered` | worker `i` sleeps `i+1` ticks |

This measures production `hrt_tick_from_isr()` behavior. The intrusive delta sleeper queue has bounded O(N) task-context insertion, O(1) no-expiry tick work and O(K) work for K expiries plus READY publication.

## Accepted development evidence

### Scheduler/lifecycle baseline

Run `20260905T134123Z_80f2042f` on SHA `80f2042f2c64053a9ea888666474c5dad5f72797` passed 11/11 functional contracts and 22/22 historical benchmarks.

### Event/notification development baseline

Run `20260905T161136Z_aa39e9bb` on SHA `aa39e9bb5f12f8ada229441a13e83d91c0dbeae6` passed:

```text
Functional: 13 / 13 PASS
Historical benchmarks: 22 / 22 PASS
Overall: PASS
```

That run proves the event/notification functional hardware contracts but predates the subsequently integrated 16-image signal timing matrix, so it is not final v0.5.0 release evidence.

## Human LED acceptance

The LED check is qualitative. PASS means both LEDs visibly toggle and their relative configured rates are distinguishable. Automated counters prove task progress; DWT benchmarks provide quantitative timing evidence.

## Result summary

The runner records:

- HardRT SHA and tracked source state
- STM32CubeH7 SHA/state
- board probe result
- functional `N/13 PASS`, failure and not-run counts
- benchmark `N/38 PASS`, failure and not-run counts
- established timing min/avg/max
- event/notification ISR and event-scan timing min/avg/max
- signal waiter count and expected/observed wake fan-out
- scheduler/PendSV breakdown
- tick/sleeper scenario/capacity/wake metadata
- RR trace/quantum evidence
- raw build/OpenOCD/GDB log locations

A default full run is the only mode intended to become complete release evidence.

See [QUALIFICATION.md](QUALIFICATION.md), [STATISTICS.md](STATISTICS.md), and [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md).
