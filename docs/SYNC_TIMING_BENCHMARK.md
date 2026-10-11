# STM32H755 synchronization/critical-section DWT benchmark

Status: **development measurement only** for HardRT #53 and #52. It does not replace the 38-image canonical benchmark matrix or count as release qualification.

## Run

From the feature branch and a clean tracked source checkout with NUCLEO-H755ZI-Q / CM7 connected by ST-Link:

```bash
git switch feature/h755-sync-critical-benchmarks
git pull --ff-only

./scripts/stm32_sync_benchmark.sh \
  /home/dev/STM32Cube/Repository/STM32CubeH7 \
  --samples 10000
```

The runner probes the board, builds a **separate instrumented Cortex-M library**, flashes the firmware, drives OpenOCD and GDB, validates the 14 result rows and writes:

```text
validation/stm32/sync_benchmarks/<UTC>_<short-SHA>/
  report.md
  sync_dwt.csv
  raw/probe.log
  raw/build_flash.log
  raw/openocd.log
  raw/gdb.log
```

All output is gitignored. The report records the exact HardRT SHA, STM32CubeH7 SHA, toolchain and clock. Send back the `report.md`, `sync_dwt.csv`, and raw logs when reviewing the results. This script needs **no account login** and never deletes a branch or publishes a release.

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

The fully unfiltered **16 functional + 38 canonical benchmark** campaign remains a **separate** frozen 0.6.0 release candidate gate.
