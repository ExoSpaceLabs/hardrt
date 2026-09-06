# STM32H755 hardware qualification evidence

Use one manual entry point for physical-board validation:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The script runs the complete NUCLEO-H755ZI-Q / CM7 qualification matrix: **13 functional contracts** plus **38 hardware benchmark images**. It owns build, flash, OpenOCD/GDB collection, result parsing, evidence capture, and the final PASS/FAIL summary.

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

## Release evidence handling

For a release candidate:

1. freeze the source SHA after hosted/cross-build CI is green;
2. run the unfiltered qualification command from that exact SHA with clean tracked HardRT source and a clean/pinned STM32CubeH7 checkout;
3. require board probe PASS, **13/13 functional PASS**, **38/38 benchmark PASS**, and Overall PASS;
4. inspect the report and raw logs;
5. retain the selected package locally under `validation/stm32/releases/vX.Y.Z/` if desired;
6. **do not commit generated qualification evidence**, because that would change the SHA that was physically qualified;
7. publish the selected qualification archive as a GitHub Release asset from the `X.Y.Z` tag that points to the qualified source commit.

The `v` prefix in the local retention directory is not part of the Git tag. Both timestamped runs and `validation/stm32/releases/` are gitignored deliberately.

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
13. BASEPRI critical-section contract

## Hardware benchmark matrix

The benchmark suite contains **38 independently built/flashed images**:

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
