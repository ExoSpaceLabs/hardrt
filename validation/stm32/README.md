# STM32H755 hardware qualification evidence

Use one manual entry point for physical-board validation:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The script runs the current NUCLEO-H755ZI-Q / CM7 qualification matrix: **16 functional contracts** plus **52 benchmark metrics across 39 images**. The published v0.5.x evidence remains the historical 13-functional-contract matrix. It owns build, flash, OpenOCD/GDB collection, result parsing, evidence capture, and the final PASS/FAIL summary.

Development runs are written under:

```text
validation/stm32/<UTC>_<short-sha>/
```

Timestamped development-run directories are ignored by Git, so they remain available for local inspection without changing the source tree.

Development shortcuts are available through the same entry point:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --only functional
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --only benchmark
```

A filtered run is useful during development but is **not** complete release evidence. Only the default unfiltered run qualifies a release candidate.

## Release qualification and reruns

The final candidate must already be integrated into `develop`; do not
qualify a temporary feature branch. From a clean source and supported
STM32CubeH7 checkout:

1. Run the **unfiltered** hardware qualification script.
2. Confirm board probe PASS, 16/16 functional PASS, 52/52 benchmark
   metrics PASS, and Overall PASS.
3. Record the tested SHA, board/configuration, date and PASS/FAIL totals
   in the release summary.
4. Merge the qualified source and `VERSION` to `main`. Green CI
   automatically tags, builds and **publishes** the software release.

The runner writes `qualification.md` and GDB/OpenOCD/build/CSV diagnostics
under a gitignored timestamped directory. These working files may be
deleted after verifying the summary: **no archive, upload, or long-term raw-log
retention is required**. Do not commit generated qualification evidence.

To regenerate measurements for a previously released version, use its
immutable tag with the appropriate physical hardware and STM32CubeH7 setup:

```bash
git switch --detach X.Y.Z
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

Re-running reproduces the test method, not the historical data: timing
measurements may vary and the new results represent a new test run.
Older packaging tools (`scripts/package_stm32_qualification.sh`,
`scripts/finalize_release.sh`) remain optional diagnostics, not release
requirements. See [release process](../../docs/RELEASE_PROCESS.md).

## Functional hardware matrix

The board/OpenOCD probe is a prerequisite and is reported separately.

1. C blinky/task integration
2. C++ blinky/task integration
3. scheduler counter demo
4. fixed-priority ISR preemption
5. global RR mixed-priority scheduling
6. `PRIORITY_RR` retained-quantum/queue-precedence validation
7. semaphore hardware contract
8. queue hardware contract
9. mutex hardware contract
10. event-flags hardware contract, including task/real-ISR producers and scheduler-aware `need_switch`
11. task-notification hardware contract, including pending/unrelated-IPC behavior and real-ISR wake
12. external TIM2-driven tick contract
13. stable periodic-delay contract (`hrt_delay_until()` phase stability + explicit miss/lateness)
14. common IPC timeout contract (semaphore success/disarm plus semaphore, queue RX/TX, mutex, event and notification expiry/unlink/recovery)
15. bounded mutex priority-inheritance contract (transitive donation through two mutexes, fixed-priority execution ordering, direct FIFO handoff)
16. BASEPRI critical-section contract

## Hardware benchmark matrix

The benchmark suite contains **39 independently built/flashed images** and
**52 mandatory metric results**. The original 38 images are preserved:

### Historical scheduler/semaphore set: 4

- DWT `event_to_task` (legacy semaphore-backed composite name)
- DWT `sem_isr_ready`
- DWT `ready_to_task`
- DWT `scheduler_decision` / PendSV decomposition

### v0.5 event/notification set: 16

- `event_isr_to_task`
- `notify_isr_to_task`
- `event_scan_none` at 1, 8, 16, and 32 registered waiters
- `event_scan_one` at 1, 8, 16, and 32 registered waiters
- `event_scan_all` at 1, 8, 16, and 32 registered waiters
- `notify_isr_no_wake`
- `notify_isr_wake`

The event-scan firmware validates the actual waiter load and expected/observed wake fan-out in addition to timing.

### Synchronization critical-section baseline: 14 metrics, 1 image

The final image measures semaphore take/give, mutex lock/unlock and queue
send/receive at 4, 16, 64, 256, 1024 bytes. Each result row is counted
independently. Missing/incomplete results fail the full suite. The image
enables its own diagnostic BASEPRI DWT profile without instrumenting normal
kernel builds. The raw log and CSV are generated for each run and may be
removed once the report has been validated.

### Tick/sleeper scaling set: 18

At configured application-task capacities 8, 16, and 32, the runner measures:

- `none`
- `one_sleep`
- `all_sleep`
- `one_expiry`
- `simultaneous`
- `staggered`

LED observations are qualitative. Automated counters prove task progress and DWT fixtures provide quantitative timing evidence.

See [`docs/STM32_MANUAL_TESTS.md`](../../docs/STM32_MANUAL_TESTS.md) and [`docs/QUALIFICATION.md`](../../docs/QUALIFICATION.md) for the authoritative release contract.
