# STM32H755 synchronization/critical-section DWT benchmark

Status: **required part of the full 0.6 release benchmark matrix** for #53 and #52. The original 38 benchmark images remain unchanged; one added diagnostic image supplies 14 additional required results. The standalone script remains a development-only scoped shortcut, not release qualification.

## Run the scoped development benchmark

From the feature branch and a clean tracked source checkout with NUCLEO-H755ZI-Q / CM7 connected by ST-Link:

```bash
git switch feature/h755-sync-critical-benchmarks
git pull --ff-only

./scripts/stm32_sync_benchmark.sh \
  /home/dev/STM32Cube/Repository/STM32CubeH7 \
  --samples 10000
```

The scoped runner probes the board, builds a **separate instrumented Cortex-M library**, flashes the firmware, drives OpenOCD and GDB, validates the 14 result rows and writes:

```text
validation/stm32/sync_benchmarks/<UTC>_<short-SHA>/
  report.md
  sync_dwt.csv
  raw/probe.log
  raw/build_flash.log
  raw/openocd.log
  raw/gdb.log
```

All output is gitignored. The report records the exact HardRT SHA, STM32CubeH7 SHA, toolchain and clock. This script needs **no account login** and never deletes a branch or publishes a release.

## Required full release campaign

After merging all candidate changes into `develop`, freeze the exact SHA,
require complete hosted/cross-build CI and run the unfiltered qualification:

```bash
./scripts/stm32_manual_test_full.sh \
  /home/dev/STM32Cube/Repository/STM32CubeH7 --clean-builds
```

The canonical runner automatically builds/flashes this same synchronization
image, parses all 14 GDB rows, counts them as 14 independent benchmark results
and stores `raw/sync_benchmark_gdb.log` plus
`raw/sync_benchmark_metrics.csv` within the main qualification tree.
The required totals are **16/16 functional + 52/52 benchmark metrics**
across **39 benchmark images**, board probe PASS, Overall PASS, clean
source and full unfiltered evidence. Neither the previous standalone 14/14
run nor `--only benchmark` is a release qualification.

## Metrics

Each case runs **32 warm-up iterations** and then the configured number of recorded samples. The 14 separate measurements are:

| ID | Metric | Payload |
|---:|---|---:|
| 0 | `hrt_sem_try_take`, token available | none |
| 1 | `hrt_sem_give`, no waiter | none |
| 2 | `hrt_mutex_try_lock`, unlocked | none |
| 3 | `hrt_mutex_unlock`, no waiter | none |
| 4–13 | `hrt_queue_try_send` and `hrt_queue_try_recv` | 4, 16, 64, 256, 1024 bytes |

All cases execute in a single priority-0 application task. The external tick source is selected but no periodic tick ISR is started. Queue capacity is one item, with separate source/destination/storage buffers; copied contents are verified. No operation blocks or performs task handoff in this baseline.

Each CSV row contains `id,bytes,count,api_min,api_avg,api_max,masked_min,masked_avg,masked_max` in **CPU cycles**, with core clock reported separately. The diagnostic profile is configured by `HARDRT_CORTEXM_CRIT_PROFILE=ON` and compiled only for the benchmark library.

**API cycles** measure around each call and include its call/return, critical-section entry/exit, and instrumentation overhead.

**Masked-window cycles** measure DWT between the port's BASEPRI-raise barrier and its BASEPRI-restore operation for the outermost critical section. This deliberately excludes mask-entry/exit instructions, includes benchmark instrumentation, and is **not** a full interrupt-masking upper bound. Nested critical sections contribute one outermost measurement, not one measurement per nested call.

These are controlled, mostly non-interfered min/mean/max **observations**, not WCET, universal latency bounds, or proof that a particular application is schedulable. DWT counters wrap at 32 bits, so each operation must take less than one wrap interval; the runner rejects impossible records.

## Interpretation and outstanding coverage

Compare increasing queue-item size against both API and masked-window cycles. The pair quantifies the cost of keeping `memcpy` inside the queue critical section for **this image**, **this clock**, **this compiler**, and **this memory/cache placement**. Choose a qualified maximum message size or pointer/index queue recommendation only after reviewing the measurements and IRQ budget.

Still outstanding before #53 can close:

- contended mutex donation, withdrawal, transitive chains and FIFO handoff;
- absolute IPC timeout arm/disarm/expiry/unlink, including simultaneous expiry fan-out;
- controlled IRQ interference and cache/memory/priority sensitivity;
- full outermost BASEPRI edge-to-edge timing that accounts for instrumentation effects;
- numeric configuration-specific mutex blocking analysis, including application-held critical sections and interference assumptions.

The fully unfiltered **16 functional + 52 benchmark metric** campaign is the
frozen 0.6.0 release candidate gate, including these 14 metrics. The earlier
38-image set is retained for historical comparisons.
