# Hardware qualification policy

HardRT separates development measurements from release qualification evidence while using one human-facing STM32 runner for both functional validation and hardware benchmarking.

## Release branch policy

A release candidate is qualified from `develop`, not from a temporary feature or release branch. All target-affecting source, build, version, package, and test changes must be merged into `develop` before physical qualification.

The physically qualified `develop` SHA is the **hardware-qualified source SHA**. Normally that exact SHA is promoted to `main` and tagged. A later release commit is permitted only when every post-qualification change is release-automation or documentation-only and therefore cannot affect the compiled HardRT library or STM32 qualification binaries. This narrow exception exists so release infrastructure can be standardized without invalidating already-completed hardware evidence.

Before using that exception, run:

```bash
python3 scripts/check_release_qualification_diff.py <qualified-sha> <release-sha>
```

The guard permits only tracked Markdown/documentation, GitHub workflow files, and the dedicated release/documentation helper scripts. Any other changed path fails the check and requires a new full hardware qualification run.

At tag time `main` and `develop` must resolve to the same release commit. Temporary feature/release branches are deleted after completion. This keeps `develop` as the single integration/release-candidate source and `main` as the released history.

## Tag-driven release publication

Release tags use the non-v-prefixed `X.Y.Z` convention. The permanent `.github/workflows/release.yml` workflow owns release publication when such a tag is pushed.

The workflow:

- validates the `X.Y.Z` tag against the CMake project version;
- requires the tag SHA to be the current `main` SHA;
- requires `main` and `develop` to be aligned;
- builds/install-packages POSIX and Cortex-M variants;
- validates an installed POSIX CMake consumer;
- creates reproducible POSIX, Cortex-M, and combined bundle archives;
- generates SHA-256 checksums;
- extracts the matching version section from `RELEASE_NOTES.md`;
- retains the generated package set as a GitHub Actions artifact;
- creates/updates the GitHub Release and publishes the generated software artifacts.

Physical qualification evidence is generated outside CI by the hardware runner and is attached to the corresponding GitHub Release separately. CI must never synthesize or reinterpret physical evidence.

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
validation/stm32/releases/X.Y.Z/
```

The local evidence directory mirrors the repository `X.Y.Z` release-tag convention.

These paths are intentionally gitignored. Generated hardware evidence must **not** be committed after qualification. The selected passing package is published as a GitHub Release artifact for the corresponding `X.Y.Z` release.

The hardware evidence records the hardware-qualified source SHA. If the final tag points to a later commit under the release-automation-only exception, the qualification record and release issue must preserve both SHAs and the qualification-diff guard must pass.

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

Earlier H755 runs established the scheduler/lifecycle and event/notification functional baselines before the complete 16-image signal timing matrix was consolidated. They are development evidence, not substitutes for a release qualification run.

Representative historical wake/switch values remain documented in [STATISTICS.md](STATISTICS.md). Release-specific signal-profile numbers belong to the selected GitHub Release qualification artifact produced from the hardware-qualified source SHA.

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

## v0.5.1 corrective release evidence contract

HardRT 0.5.1 corrects the hosted POSIX execution backend, but it is still a new release source tree. The published 0.5.0 STM32 package remains historical evidence and is not reused as physical qualification for 0.5.1.

For 0.5.1:

1. merge every target/build-affecting 0.5.1 source, version, package, and test change into `develop`;
2. freeze one `develop` SHA and require all hosted/cross-build release CI to pass on that candidate;
3. run the default unfiltered STM32 qualification command on that SHA;
4. require board/OpenOCD probe PASS, **13/13 functional PASS**, **38/38 benchmark PASS**, and Overall PASS;
5. retain the generated qualification package outside the tracked source tree;
6. if no tracked changes follow, promote/tag that exact SHA;
7. if only release automation/documentation must change, apply those changes, run `scripts/check_release_qualification_diff.py` against the hardware-qualified SHA, require hosted/cross-build/documentation CI to remain green, and record both SHAs in the release tracker;
8. align `main` and `develop`, create tag `0.5.1`, and let `.github/workflows/release.yml` publish the generated software artifacts;
9. attach the retained physical qualification package and checksum to the GitHub Release.

The full physical run is required even though most 0.5.1 implementation work is POSIX-specific. In addition, 0.5.1 changes the shared public external-tick entry so functional case 12 (`hrt_tick_from_isr()` driven by TIM2) is directly relevant hardware evidence.

Any post-qualification change outside the release-automation/documentation whitelist changes the qualified product and requires a new full qualification run.

## v0.5.0 release evidence contract

The following records the policy used for the already-published 0.5.0 release.

Before the hardware run, all release-facing source/docs/version changes had to be complete on `develop` and hosted CI had to pass on one frozen `develop` SHA.

The final STM32 package then had to be generated from that exact SHA and record at minimum:

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

After that run passed, `main` was fast-forwarded to the exact qualified `develop` SHA, tag `0.5.0` was created on `main`, and the qualification package and release binaries were published from that tag. Any later tracked change belongs to a later candidate and must be qualified under that release's policy.

## Human observation

The two blinky cases retain a qualitative human check: both LEDs must visibly toggle and their configured relative rates must be distinguishable. Exact millisecond timing is not a human acceptance criterion.

## Relationship to broader hard-real-time qualification

Passing the v0.5 matrix demonstrates the tested H755 configuration satisfies the current behavioral contracts and provides measured timing evidence for the benchmark workloads. It does not establish universal WCET bounds. Analytical critical-section bounds, bounded mutex priority inversion, queue-copy scaling, richer interference analysis, and complete machine-readable timing evidence remain 1.0-quality work.
