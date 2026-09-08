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

A release candidate must already be on `develop`; do not qualify a temporary feature or release branch.

For a release candidate:

1. merge all target/build-affecting changes into `develop`;
2. freeze the `develop` SHA after hosted/cross-build CI is green;
3. run the unfiltered qualification command from that exact SHA with clean tracked HardRT source and a clean/pinned STM32CubeH7 checkout;
4. require board probe PASS, **13/13 functional PASS**, **38/38 benchmark PASS**, and Overall PASS;
5. inspect the report and raw logs;
6. package the selected evidence without adding it to Git:

   ```bash
   ./scripts/package_stm32_qualification.sh X.Y.Z validation/stm32/<UTC>_<short-sha>
   ```

7. promote/tag the release according to `docs/QUALIFICATION.md`; release-automation/documentation-only follow-up changes are allowed only through the repository qualification-diff guard;
8. let `.github/workflows/release.yml` create the draft release and software assets;
9. publish and verify the physical evidence, then clean temporary branches:

   ```bash
   ./scripts/finalize_release.sh \
     X.Y.Z \
     validation/stm32/<UTC>_<short-sha> \
     --cleanup-branches
   ```

The package helper creates a deterministic `.tar.xz` archive plus a separate SHA-256 file under `validation/stm32/releases/X.Y.Z/`. Both timestamped runs and local release packages are gitignored deliberately. **Do not commit generated qualification evidence.**

The canonical physical release assets are:

```text
hardrt-stm32-qualification-X.Y.Z.tar.xz
hardrt-stm32-qualification-X.Y.Z.tar.xz.sha256
```

The archive contains the original `qualification.md`, every raw build/OpenOCD/GDB log, and package metadata identifying the release and qualified source SHA. The GitHub Release asset contains the evidence; the repository does not contain hundreds of generated log files.

See [`docs/RELEASE_PROCESS.md`](../../docs/RELEASE_PROCESS.md) for the complete procedure.

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
