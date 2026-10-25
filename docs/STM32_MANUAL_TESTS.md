# STM32H755 Manual Validation

HardRT's hosted Linux/POSIX tests are automatic. STM32H755 runtime validation remains manual until a hardware CI runner exists. Cross-compilation proves firmware builds and links; it does not prove interrupt, PendSV, GPIO, clock, synchronization, or board behavior.

The supported manual target is NUCLEO-H755ZI-Q, exercising CM7 while CM4 is held in reset.

## One human-facing runner

Run from the repository root:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

This is the canonical **release qualification** command. It owns build, flash, OpenOCD/GDB sessions, functional validation, all timing/profile images, result parsing, evidence capture, and final console/report summaries. Event/notification profiling is part of this runner rather than a second procedure.

### Modes

```text
(no --only)       = functional + benchmark
--only functional = functional only
--only benchmark  = benchmark only
```

The board probe always runs first. Only the default unfiltered mode is release-candidate evidence; filtered modes are development shortcuts.

## Synchronization timing: integrated release matrix and scoped development shortcut

The canonical full/unfiltered runner includes all 14 synchronization metrics
automatically within its benchmark suite. During development, just the same
[STM32 synchronization image](SYNC_TIMING_BENCHMARK.md) can be run separately:

```bash
./scripts/stm32_sync_benchmark.sh \
  /home/dev/STM32Cube/Repository/STM32CubeH7 --samples 10000
```

The canonical suite includes **52 required benchmark metrics across 39 firmware
images**: the historical 38 measurements/images plus the new 14 synchronized
metrics in one additional build/flash image. The standalone command is a
**development shortcut only**. It uses the same opt-in instrumented library
and captures a separate SHA-keyed report/CSV under
`validation/stm32/sync_benchmarks/`. Neither runner needs credentials or
mutates remote branches.

## Release branch and evidence handling

The release candidate must already be merged into `develop`. Temporary feature/release branches are not qualification sources.

Development runs are written under:

```text
validation/stm32/<UTC>_<short-sha>/
```

For a release candidate, the selected passing package is retained locally under:

```text
validation/stm32/releases/X.Y.Z/
```

The local evidence directory mirrors the repository `X.Y.Z` release-tag convention.

Both development and release-evidence directories are gitignored deliberately. **Do not commit generated qualification evidence after the board run.** Hundreds of raw logs belong in the release evidence archive, not in Git history.

Qualify the final source on the physical board **before** promoting it to `main`. Record the tested source SHA in `qualification.md` and retain the raw logs locally. A later merge commit has a different SHA from the directly tested source; do not describe it as the physical test commit. The standalone `check_release_qualification_diff.py` helper remains available to review build-input changes after a hardware run, but is not a second publication action.

Once the qualified source and root `VERSION` are on `main`, green Linux, Signal stress and Documentation CI automatically creates the immutable tag, builds architecture-specific software assets and **publishes** the release. No manual draft approval, evidence attachment, or local finalizer is required to publish. CI does not fabricate or upload the operator's physical logs.

## Package the physical evidence

After a full passing run, package the selected run directory with:

```bash
./scripts/package_stm32_qualification.sh \
  X.Y.Z \
  validation/stm32/<UTC>_<short-sha>
```

The packaging helper rejects dirty, filtered, failed, incomplete, or partially executed reports. It requires:

- clean tracked HardRT source;
- unfiltered functional + benchmark mode;
- board/OpenOCD probe PASS;
- every reported functional contract passed, with zero failures and zero not-run cases;
- every reported benchmark passed, with zero failures and zero not-run cases;
- Overall PASS;
- completed `scripts/stm32_manual_test_full.sh` report;
- a valid HardRT qualification SHA present in the repository.

It creates:

```text
validation/stm32/releases/X.Y.Z/
  hardrt-stm32-qualification-X.Y.Z.tar.xz
  hardrt-stm32-qualification-X.Y.Z.tar.xz.sha256
```

The `.tar.xz` contains the complete `qualification.md`, all raw build/OpenOCD/GDB logs, and package metadata identifying the release version, source run, and hardware-qualified SHA. Packaging is deterministic for the same evidence tree and qualified commit.

The unpacked evidence directory and generated archive remain untracked.

## Optional local evidence verification

Release publication is already automatic. For additional local verification, run:

```bash
./scripts/finalize_release.sh \
  X.Y.Z \
  validation/stm32/<UTC>_<short-sha>
```

The optional finalizer verifies the immutable release tag is on `main` history, the qualified source ancestry and build-input equivalence, and the retained physical evidence archive/checksum. It never publishes releases, uploads assets or deletes branches. All release publication occurs automatically in GitHub Actions, independently of this local archival helper.

The canonical future software assets are architecture-qualified:

```text
hardrt-posix-linux-amd64-X.Y.Z.tar.gz
hardrt-posix-linux-arm64-X.Y.Z.tar.gz
hardrt-cortexm-X.Y.Z.tar.gz
SHA256SUMS
```

The POSIX packages are built and tested natively on their respective Linux architectures before publication. There is no combined bundle artifact.

See [RELEASE_PROCESS.md](RELEASE_PROCESS.md) for the complete standardized release sequence.

## Common requirements

- ARM GNU bare-metal toolchain (`arm-none-eabi-*`)
- OpenOCD with ST-Link support
- `gdb-multiarch` or `arm-none-eabi-gdb`
- local STM32CubeH7 checkout supplied to the runner
- physical NUCLEO-H755ZI-Q connected through ST-Link

For release evidence, use clean tracked HardRT source and a clean recorded STM32CubeH7 checkout.

## Functional validation: 16 contracts

The board/OpenOCD probe is a prerequisite, not a functional feature. The current 0.6 development matrix runs **16 behavior contracts**. The published v0.5.x evidence remains the historical 13-contract matrix:

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
13. **Stable periodic delay hardware contract**: one persistent phase drives repeated five-tick releases while two ticks per period are consumed as task execution; exact-deadline behavior is on time; an intentional three-tick late call returns `HRT_DELAY_MISSED` with the expected lateness and does not sleep or rebase phase.
14. **Common IPC timeout hardware contract**: one external tick domain drives a producer-before-deadline semaphore success plus semaphore, queue receive/send, mutex, event, notification-wait and notification-take expiries; timed-out waiter membership is removed and notification operations recover after timeout.
15. **Bounded mutex priority inheritance hardware contract**: transitive donation, release ordering and handoff.
16. **BASEPRI critical-section contract**: unmasked/weaker/stricter/nested entry cases preserve the HardRT ceiling and exact pre-entry mask state.

Timing measurements are deliberately not counted as functional contracts.

### External tick startup rule

The application owns an external tick source. It must not begin routing periodic interrupts into HardRT before scheduler execution has started. The H755 validator configures TIM2 before scheduler start but enables the timer from the first dispatched application task.

## Hardware benchmark suite: 52 metrics / 39 images

Benchmark mode runs **39 build/flash images** producing **52 mandatory
benchmark metrics**. The previously qualified 38-image set remains unchanged:

- 4 established scheduler/semaphore latency and switch measurements;
- 16 v0.5 event/notification signal measurements;
- 18 tick/sleeper scaling measurements;
- **14 synchronization/BASEPRI-mask/queue-copy metrics** from one diagnostic image.

Every image is rebuilt and flashed independently. The diagnostic image
enables `HARDRT_CORTEXM_CRIT_PROFILE` only for itself; the normal kernel
remains uninstrumented.

The synchronization image produces 14 result rows (semaphore take/give,
uncontended mutex lock/unlock, queue send/receive at 4/16/64/256/1024 bytes).
It passes only when the firmware reports PASS, all 14 metric IDs and sizes
are present and every row contains the configured number of valid samples.
Each row is an independent PASS/FAIL case in the complete qualification report.
Raw `sync_benchmark_gdb.log` and `sync_benchmark_metrics.csv` are retained
inside the canonical release evidence archive. Failure of the diagnostic
image fails all 14 metrics rather than silently skipping them.

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

Earlier H755 runs established the scheduler/lifecycle and event/notification functional baselines before the complete 16-image signal timing matrix was integrated. Those runs remain useful historical development evidence but are not substitutes for release evidence generated from a frozen release-candidate source SHA.

## Human LED acceptance

The LED check is qualitative. PASS means both LEDs visibly toggle and their relative configured rates are distinguishable. Automated counters prove task progress; DWT benchmarks provide quantitative timing evidence.

## Result summary

The runner records:

- HardRT SHA and tracked source state
- STM32CubeH7 SHA/state
- board probe result
- functional `N/16 PASS`, failure and not-run counts
- benchmark `N/52 PASS`, failure and not-run counts
- established timing min/avg/max
- event/notification ISR and event-scan timing min/avg/max
- signal waiter count and expected/observed wake fan-out
- scheduler/PendSV breakdown
- tick/sleeper scenario/capacity/wake metadata
- synchronization masked-window and API-cycle min/avg/max per size/operation
- RR trace/quantum evidence
- raw build/OpenOCD/GDB log locations

A default full run is the only mode intended to become complete release evidence.

See [QUALIFICATION.md](QUALIFICATION.md), [RELEASE_PROCESS.md](RELEASE_PROCESS.md), [STATISTICS.md](STATISTICS.md), and [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md).
