# STM32H755 Manual Validation

HardRT's hosted Linux/POSIX tests are automatic. STM32H755 runtime validation remains manual until a hardware CI runner exists. Cross-compilation proves that firmware builds and links; it does not prove interrupt, PendSV, GPIO, clock, synchronization, or board behavior.

The supported manual target is currently NUCLEO-H755ZI-Q, exercising CM7 while CM4 is held in reset.

## One human-facing runner

Run from the repository root:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7
```

The runner owns build, flash, OpenOCD/GDB sessions, result parsing, evidence capture, and final console/report summaries.

Do not manually chain the individual build/GDB helper scripts for normal qualification. They remain implementation details used by the runner and CI.

### Modes

The mode logic is symmetric:

```text
(no --only)       = functional + benchmark
--only functional = functional only
--only benchmark  = benchmark only
```

The board probe always runs first and is reported separately.

Examples:

```bash
# Complete hardware run. This is the release-candidate path.
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --clean-builds

# Repeat only behavior/functional validation.
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --only functional

# Run every hardware benchmark, each as its own build/flash image.
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --only benchmark
```

The default run is genuinely complete. Filtered modes exist only to avoid repeating unrelated hardware work during development.

## Evidence location

By default each run is written visibly under:

```text
validation/stm32/<UTC>_<short-sha>/
```

Timestamped development directories are gitignored.

For a release candidate, run the default full mode from the exact final SHA. After inspection, manually copy or move the chosen run to:

```text
validation/stm32/releases/vX.Y.Z/
```

There is no separate qualification wrapper and no promotion script.

## Common requirements

- ARM GNU bare-metal toolchain (`arm-none-eabi-*`).
- OpenOCD with ST-Link support.
- `gdb-multiarch` or `arm-none-eabi-gdb`.
- local STM32CubeH7 checkout supplied to the runner.
- physical NUCLEO-H755ZI-Q connected through ST-Link.

For release evidence, use clean tracked HardRT source and a clean recorded STM32CubeH7 checkout.

## Functional validation: 9 contracts

The board probe is a prerequisite, not a functional feature. After it passes, functional mode runs nine behavior contracts:

1. **C blinky**
   - both task counters advance;
   - `g_example_error == 0`;
   - both LEDs visibly toggle;
   - their configured relative rates are visibly distinguishable.

2. **C++ blinky**
   - same acceptance principle as the C example using the C++ API.

3. **Scheduler counter demo**
   - both task-entry and post-sleep counters advance;
   - no example error.

4. **Fixed-priority hardware preemption**
   - TIM2 ISR wakes a blocked higher-priority task while lower-priority Thread mode is CPU-bound;
   - high-priority task must execute before interrupted low-priority Thread mode continues.

5. **`PRIORITY_RR` retained-quantum preemption**
   - required trace is:

   ```text
   low-A -> IRQ -> high -> low-A -> low-B
   ```

   - asynchronous higher-priority preemption must not rotate low-A behind same-priority low-B;
   - low-A must retain its unused RR quantum.

6. **Semaphore hardware contract**
   - counting/saturation behavior;
   - actual TIM2 ISR wakes blocked higher-priority waiter;
   - `need_switch` reports scheduler-preemption need correctly.

7. **Queue hardware contract**
   - FIFO/full/empty behavior;
   - ISR send wakes blocked receiver with payload preserved;
   - ISR receive wakes blocked sender with payload/order preserved;
   - priority handoff occurs before lower-priority continuation.

8. **Mutex hardware contract**
   - ownership and blocking;
   - direct ownership handoff on unlock;
   - higher-priority new owner executes before lower-priority unlocker continues;
   - mutex remains task-context only.

9. **External tick hardware contract**
   - SysTick disabled for the test;
   - TIM2 periodic IRQ drives `hrt_tick_from_isr()`;
   - sleep duration matches requested tick count;
   - awakened high-priority task preempts lower-priority work;
   - `hrt_now_ms()` and kernel tick agree for the 1 kHz fixture.

Timing measurements are deliberately not counted as functional contracts. Adding a new benchmark must not make the kernel appear to have acquired another functional feature.

## Hardware benchmark suite: 22 images

Benchmark mode currently runs 22 independent hardware images: four latency/switch benchmarks and an 18-point tick/sleeper scaling matrix.

### Latency and switch benchmarks

1. **`event_to_task`**
   - composite software ISR point to awakened task continuation;
   - application-side DWT timestamps;
   - HardRT timing profile remains `none`.

2. **`sem_isr_ready`**
   - ISR semaphore call entry to waiter READY transition;
   - built with the private `ipc` timing profile and the required hook header.

3. **`ready_to_task`**
   - waiter READY transition to continuation after blocked `hrt_sem_take()` returns;
   - built with the private `ipc` timing profile and waiter-READY hook;
   - includes ISR tail, scheduler/context switch, exception return and blocked API continuation;
   - not a pure context-switch microbenchmark.

4. **`scheduler_decision` / PendSV decomposition**
   - built as a diagnostic-only timing image;
   - uses the unmodified production `hrt__schedule()`;
   - substitutes a measurement-only PendSV handler in that benchmark image.

The runner calls `scripts/build-lib-stm32h7xx-dwt-timing.sh` separately for each of these four images. Instrumentation is therefore a property of the benchmark image, not of normal HardRT builds.

### Tick/sleeper scaling matrix

The matrix was introduced to measure the former per-tick full-TCB sleeper scan and is intentionally retained after its replacement. It therefore provides a direct before/after contract instead of changing the benchmark whenever the implementation changes.

Benchmark mode rebuilds HardRT at application-task capacities:

```text
8, 16, 32
```

For each capacity it runs six workloads:

| Scenario | Worker state / expiry pattern |
|---|---|
| `none` | all worker slots occupied by semaphore-blocked tasks; no sleepers |
| `one_sleep` | one worker sleeps beyond the 10k-sample window; remaining workers block |
| `all_sleep` | all workers sleep beyond the sample window |
| `one_expiry` | one worker expires every tick and sleeps again for one tick |
| `simultaneous` | every worker expires on the same tick and sleeps again for one tick |
| `staggered` | worker `i` sleeps for `i+1` ticks, distributing expiries |

This produces **18 separate measurements**. Each image reports min/avg/max for:

```c
hrt_tick_from_isr();
```

The measurement points are application-side DWT reads immediately before and after that production API call. These images use:

```text
HARDRT_TIMING_PROFILE=none
```

so the tick scaling result contains no kernel timing hooks and no replacement tick implementation.

TIM2 is one-shot for this matrix. After the measured tick call returns, the ISR wakes a lower-priority benchmark driver outside the measured interval. Any worker made READY by the tick has higher priority than that driver, so the worker runs and returns to its intended sleep/block state before the driver rearms the next sample. This ensures each sample begins from the workload state named by the scenario.

The task capacity is a real HardRT build configuration, not merely a runtime worker count. `HARDRT_CFG_MAX_TASKS=8`, `16`, or `32` is passed when the library is built, then the benchmark fills all application slots with one driver/setup task and the remaining workers.

`worker_wakes` is included in the debugger result as a workload sanity check. For the 10,000-sample one-shot fixture:

- non-expiry scenarios must report zero wake completions;
- `one_expiry` should report 9,999 completed wakes;
- `simultaneous` should report `worker_count * 9,999`;
- `staggered` should match the sum of the individual periodic wake counts.

#### Baseline and current expectation

The pre-delta baseline is physical run `20260903T212538Z_4ab7709a`, which completed **9/9 functional PASS and 22/22 benchmark PASS**. Average tick cycles were:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 902 | 1050 | 1124 | 1367 | 2282 | 1700 |
| 16 | 1389 | 1506 | 1761 | 1808 | 3990 | 2435 |
| 32 | 1954 | 2597 | 3455 | 2952 | 8676 | 4413 |

That baseline demonstrated material O(N) work even without expiries. At 32 application tasks, `none` averaged 1954 cycles and `all_sleep` averaged 3455 cycles.

Current `develop` replaces the scan with a static intrusive delta sleeper queue. Its intended bounds are:

```text
hrt_sleep() insertion : O(N), bounded task-context work
no-expiry tick        : O(1)
K expiries            : O(K)
sleeper metadata      : O(N), static
```

Equal deadlines use zero-delta followers and preserve FIFO wake order. Relative deltas make sleeper ordering independent of 32-bit absolute tick wrap. Hosted tests cover equal deadlines, staggered ordering, repeated sleep/wake cycles, and wraparound ordering.

The post-change hardware run must use this **same 18-point matrix**. Acceptance is not a magic target cycle count: `none`, `one_sleep`, and `all_sleep` should stop growing materially with configured capacity, while `simultaneous` may still grow with the number of tasks that actually wake. Until that physical A/B exists, the documented delta-queue improvement is a structural bound, not a claimed measured speedup.

### Scheduler/PendSV benchmark output

The scheduler benchmark reports:

```text
pendsv_save
scheduler_decision
pendsv_restore
pendsv_software
pendsv_to_task
derived_software_other_avg
derived_return_and_api_avg
```

Meaning:

- `pendsv_save`: PendSV software entry to outgoing `r4-r11` save completion;
- `scheduler_decision`: direct execution time of `hrt__schedule()`;
- `pendsv_restore`: scheduler return to incoming `r4-r11`/PSP restoration;
- `pendsv_software`: PendSV software entry to incoming context restored;
- `pendsv_to_task`: PendSV entry to awakened task continuation after the blocked API returns.

The derived values are engineering decompositions, not WCET bounds.

The diagnostic handler performs extra DWT reads and stores. In particular, `pendsv_to_task - pendsv_software` includes diagnostic bookkeeping as well as exception return/API continuation and must not be interpreted as pure production exception-return latency.

## Human LED acceptance

The LED check is intentionally qualitative. A human is not expected to confirm exact 100 ms, 250 ms, or 500 ms periods by eye.

PASS means:

- both LEDs visibly toggle;
- the relative speed difference is obvious and consistent with configuration, for example one is roughly twice as fast.

Automated task counters prove progress. DWT benchmarks provide quantitative timing evidence.

## 2026-09-03 duplicate-PendSV regression proof

The benchmark instrumentation was introduced to explain why the Cortex-M composite response had risen from roughly the old 1.2k-cycle region to roughly 1.7k cycles.

Before the port fix (`261e4a8e`):

```text
event_to_task avg       = 1705 cycles
sem_isr_ready avg       =  328 cycles
ready_to_task avg       = 1336 cycles
scheduler_decision avg  =  331 cycles
pendsv_software avg     =  430 cycles
pendsv_to_task avg      = 1359 cycles
derived return/API tail =  929 cycles
```

The Cortex-M port implemented both halves of the task-context reschedule contract by calling `_pend_pendsv()`:

```c
hrt__pend_context_switch();
hrt_port_yield_to_scheduler();
```

That created a redundant second PendSV request on task-context blocking/yield paths.

Commit:

```text
12f745673f8ce5069903eab314b38e43591b0899
perf(hardrt): avoid duplicate Cortex-M PendSV request
```

removed the second Cortex-M hardware request while leaving POSIX behavior and scheduler semantics unchanged.

After the fix (`12f74567`):

```text
event_to_task avg       = 1183 cycles
sem_isr_ready avg       =  334 cycles
ready_to_task avg       =  780 cycles
scheduler_decision avg  =  329 cycles
pendsv_software avg     =  429 cycles
pendsv_to_task avg      =  680 cycles
derived return/API tail =  251 cycles
```

At the time of that A/B run, the then-current 13-check hardware matrix passed completely. The runner has since been reorganized so timing cases are reported as benchmarks rather than functional features; the underlying hardware evidence is unchanged.

The A/B result is important: scheduler decision and PendSV software cost stayed essentially unchanged while the excess tail dropped by roughly 678 cycles. This is direct evidence that the dominant regression came from the redundant reschedule request rather than the scheduler-correctness logic itself.

Full evidence and interpretation are recorded in `docs/STATISTICS.md`.

## Fixed-priority and PRIORITY_RR acceptance detail

The preemption fixture exports a trace and result variables so pass/fail is deterministic under GDB.

Strict priority requires the beginning of the sequence to represent:

```text
low-A -> IRQ -> high -> resumed low-A
```

The `PRIORITY_RR` case requires:

```text
low-A -> IRQ -> high -> low-A -> low-B
```

with:

```text
expected_remaining = configured_quantum - (irq_tick - a_start_tick)
observed_remaining = b_first_tick - a_resume_tick
```

One tick of boundary tolerance is accepted. A higher-priority asynchronous interruption must preserve the interrupted task's same-priority queue precedence and remaining quantum.

## Result summary

The runner prints and writes:

- HardRT SHA and tracked source state;
- STM32CubeH7 SHA/state;
- board probe result;
- functional `N/9 PASS`, failure and not-run counts;
- benchmark `N/22 PASS`, failure and not-run counts;
- benchmark timing min/avg/max;
- scheduler/PendSV breakdown;
- tick/sleeper matrix scenario/capacity/wake metadata;
- PRIORITY_RR trace/quantum evidence;
- raw build/OpenOCD/GDB log locations.

A default full run is the only run mode intended to become complete release evidence. Filtered modes are development shortcuts.
