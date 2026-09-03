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
(no --only)       = functional + scheduler
--only functional = functional only
--only scheduler  = scheduler only
```

Examples:

```bash
# Complete hardware run. This is the release-candidate path.
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --clean-builds

# Repeat only the 13 functional contracts.
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --only functional

# Repeat only the scheduler/PendSV timing investigation.
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --only scheduler
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

## Functional matrix: 13 cases

The 13 functional cases are:

1. **Board probe**
   - OpenOCD connects to the STM32H755 before other tests run.

2. **C blinky**
   - both task counters advance;
   - `g_example_error == 0`;
   - both LEDs visibly toggle;
   - their configured relative rates are visibly distinguishable.

3. **C++ blinky**
   - same acceptance principle as the C example using the C++ API.

4. **Scheduler counter demo**
   - both task-entry and post-sleep counters advance;
   - no example error.

5. **DWT `event_to_task`**
   - composite software ISR-to-awakened-task response;
   - 10,000 samples by default;
   - finite ordered min/avg/max and no example/kernel error.

6. **DWT `sem_isr_ready`**
   - ISR semaphore call entry to waiter READY transition;
   - isolates ISR synchronization/wake work from later scheduling/return work.

7. **DWT `ready_to_task`**
   - waiter READY transition to continuation after blocked `hrt_sem_take()` returns;
   - includes ISR tail, scheduler/context switch, exception return and blocked API continuation;
   - not a pure context-switch microbenchmark.

8. **Fixed-priority hardware preemption**
   - TIM2 ISR wakes a blocked higher-priority task while lower-priority Thread mode is CPU-bound;
   - high-priority task must execute before interrupted low-priority Thread mode continues.

9. **`PRIORITY_RR` retained-quantum preemption**
   - required trace is:

   ```text
   low-A -> IRQ -> high -> low-A -> low-B
   ```

   - asynchronous higher-priority preemption must not rotate low-A behind same-priority low-B;
   - low-A must retain its unused RR quantum.

10. **Semaphore hardware contract**
    - counting/saturation behavior;
    - actual TIM2 ISR wakes blocked higher-priority waiter;
    - `need_switch` reports scheduler-preemption need correctly.

11. **Queue hardware contract**
    - FIFO/full/empty behavior;
    - ISR send wakes blocked receiver with payload preserved;
    - ISR receive wakes blocked sender with payload/order preserved;
    - priority handoff occurs before lower-priority continuation.

12. **Mutex hardware contract**
    - ownership and blocking;
    - direct ownership handoff on unlock;
    - higher-priority new owner executes before lower-priority unlocker continues;
    - mutex remains task-context only.

13. **External tick hardware contract**
    - SysTick disabled for the test;
    - TIM2 periodic IRQ drives `hrt_tick_from_isr()`;
    - sleep duration matches requested tick count;
    - awakened high-priority task preempts lower-priority work;
    - `hrt_now_ms()` and kernel tick agree for the 1 kHz fixture.

## Human LED acceptance

The LED check is intentionally qualitative. A human is not expected to confirm exact 100 ms, 250 ms, or 500 ms periods by eye.

PASS means:

- both LEDs visibly toggle;
- the relative speed difference is obvious and consistent with configuration, for example one is roughly twice as fast.

Automated task counters prove progress. DWT cases provide quantitative timing evidence.

## Scheduler/PendSV diagnostics

The scheduler mode uses the same ISR-wake workload but substitutes a measurement-only PendSV handler in the diagnostic image. Production HardRT scheduler code is not replaced.

It reports:

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

## 2026-09-03 duplicate-PendSV regression proof

The scheduler diagnostic was introduced to explain why the current Cortex-M composite response had risen from roughly the old 1.2k-cycle region to roughly 1.7k cycles.

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

The functional hardware matrix remained **13/13 PASS**.

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
- each functional case result;
- functional `N/13 PASS`, failure and not-run counts;
- timing min/avg/max;
- scheduler/PendSV breakdown when scheduler mode is included;
- PRIORITY_RR trace/quantum evidence;
- raw build/OpenOCD/GDB log locations.

A default full run is the only run mode intended to become complete release evidence. Filtered modes are development shortcuts.
