# Documentation Summary

This documentation set describes the HardRT **0.6.0 candidate** on `develop`.
Published `0.5.x` behavior is preserved as explicitly historical material.
Root `VERSION` is the authoritative build/package version.

## 0.6 hardware qualification and release contract

The full reference NUCLEO-H755ZI-Q CM7 gate is **16 functional contracts
and 52 DWT benchmark metrics across 39 build/flash images**. Run only the
unfiltered canonical command for release acceptance:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The source SHA physically qualified on frozen `develop` is recorded in raw
evidence. A normal reviewed `develop -> main` merge produces a different
release/tag SHA; `scripts/check_release_qualification_diff.py` permits this
**only if target build and test inputs are equivalent**. Documentation-only
changes may differ. Any changed kernel, public header, version, CMake,
toolchain, linker/startup, workflow or qualification input invalidates the
previous qualification result and requires another full physical run.

After successful current-main Linux, Signal stress and Documentation CI,
`.github/workflows/tag-release.yml` automatically tags the root `VERSION`
(with no `v` prefix) only for an operator-approved qualified release PR.
The tagger calls reusable `.github/workflows/release.yml`, which validates
POSIX and Cortex-M software packages and creates a **draft** GitHub Release.
The human operator locally packages/verifies H755 evidence with
`scripts/finalize_release.sh`, attaches the hardware archive and checksum,
and decides when to publish. CI does not synthesize or upload HIL results.
Release branch cleanup is manual.

The complete sequence is in [RELEASE_PROCESS.md](RELEASE_PROCESS.md).
Numerical cross-lane interference and universal WCET claims remain future
qualification work, not implied by passing the current physical matrix.

## Main documents

| File | Purpose |
|---|---|
| [Main README](../README.md) | Project overview and current behavior |
| [INTRODUCTION.md](INTRODUCTION.md) | Scope and design goals |
| [HARD_REAL_TIME.md](HARD_REAL_TIME.md) | Qualification boundaries and remaining work |
| [QUALIFICATION.md](QUALIFICATION.md) | Hardware evidence policy |
| [RELEASE_PROCESS.md](RELEASE_PROCESS.md) | VERSION, auto-tagging, evidence and publication |
| [STM32_MANUAL_TESTS.md](STM32_MANUAL_TESTS.md) | STM32H755 validation matrix |
| [BUILD.md](BUILD.md) | CMake and installed package |
| [API_C.md](API_C.md) | Public C API |
| [CPP.md](CPP.md) | C++17 wrappers |
| [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md) | Event and notification APIs |
| [SCHEDULING.md](SCHEDULING.md) | Scheduler and preemption behavior |
| [PORTING.md](PORTING.md) | Port contract |
| [TICK_SOURCE.md](TICK_SOURCE.md) | Internal and external ticks |
| [SEMAPHORES.md](SEMAPHORES.md) | Binary/counting + timed take |
| [MUTEXES.md](MUTEXES.md) | Mutexes, PI and timed lock |
| [QUEUES.md](QUEUES.md) | Queue payload copies and finite waits |
| [EXAMPLES_C.md](EXAMPLES_C.md) | Portable and board examples |
| [MODULE_STATUS.md](MODULE_STATUS.md) | Source/module status |
| [TESTS_POSIX.md](TESTS_POSIX.md) | Hosted test behavior |
| [SYNC_TIMING_BENCHMARK.md](SYNC_TIMING_BENCHMARK.md) | DWT queue/critical-section costs |
| [STATISTICS.md](STATISTICS.md) | Historical physical timing evidence |
| [COMPATIBILITY.md](COMPATIBILITY.md) | Pre-1.0 API/ABI boundary |
| [ROADMAP.md](ROADMAP.md) | Remaining roadmap work |
| [DOCUMENTATION.md](DOCUMENTATION.md) | Documentation CI and Doxygen |

## Current synchronization surface

- binary/counting semaphores with absolute-tick timed take;
- owner-tracked non-recursive mutexes with timed lock, FIFO handoff and
  bounded transitive priority inheritance under priority-aware scheduling;
- fixed-capacity, copy-based queues with timed task send/receive;
- statically allocated 32-bit event flags and per-task notifications with
  ISR producers and shared finite-wait variants;
- one common bounded intrusive deadline engine with no runtime heap allocation.

Priority inheritance does not bound inversion under **global RR**.
Queue `memcpy()` remains inside the critical section, and application
payload size/IRQ latency must be analyzed. See #53 for timing depth.

## Accuracy rule

Public headers, source, tests, examples and docs form one contract. A mismatch
is a defect, not a second equally valid version of the current API.
