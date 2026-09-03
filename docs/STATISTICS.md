# HardRT Timing and Latency Measurements

This document records hardware measurements for HardRT on STM32H755 Cortex-M7. The values characterize the stated configuration and workload. They are **measured observations**, not universal WCET or end-to-end hard-real-time bounds.

## Common target

- MCU: STM32H755, Cortex-M7 core
- Board used for current qualification: NUCLEO-H755ZI-Q
- Core clock: 64 MHz (`SystemCoreClock = 64_000_000`)
- HardRT port: `cortex_m`
- Build: Release
- Semihosting: disabled
- Current timing sample count: 10,000 per case

For hard-real-time claims, also record compiler/linker flags, cache/FPU/flash placement, interrupt load and priorities, memory placement, board revision, and all assumptions that can change execution time.

---

# v0.5 development: scheduler latency regression and correction

On 2026-09-03 a scheduler-correctness update was followed by an apparent Cortex-M response-time regression. The regression was investigated with progressively narrower DWT measurements instead of assuming that the scheduler logic itself was necessarily too slow.

## Relevant timing metrics

The current timing fixture defines:

- `event_to_task`: timestamp inside the TIM2 ISR before the semaphore wake path to continuation of the awakened task after the blocked semaphore API returns. This is a composite software ISR-to-task metric, not hardware interrupt latency.
- `sem_isr_ready`: ISR semaphore-call entry to the waiter becoming READY.
- `ready_to_task`: waiter READY to continuation after the blocked semaphore API returns.
- `scheduler_decision`: direct measured execution of `hrt__schedule()` in the diagnostic image.
- `pendsv_save`: diagnostic PendSV entry to outgoing `r4-r11` save completion.
- `pendsv_restore`: scheduler return to incoming `r4-r11`/PSP restoration.
- `pendsv_software`: diagnostic PendSV software entry to incoming context restored.
- `pendsv_to_task`: diagnostic PendSV entry to awakened task continuation after the blocked API returns.

The scheduler diagnostic substitutes a measurement-only PendSV handler. Production scheduler code is unchanged, but the diagnostic handler adds DWT reads and result bookkeeping. Therefore its segment values are useful for decomposition, not exact production instruction-count accounting.

## Before the Cortex-M port fix

Development run:

```text
Run ID: 20260903T201000Z_261e4a8e
HardRT: 261e4a8e2d98bde655e928a669d9cb370df29273
Tracked source: clean
STM32CubeH7: f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
Samples: 10000
Functional hardware matrix: 13/13 PASS
Development archive SHA-256:
44ee9939b4afd126ed68903aa5d99beac7933ead8755e13a904896ad6f0d134f
```

Production-path measurements:

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` | 1659 | **1705** | 2663 |
| `sem_isr_ready` | 319 | **328** | 339 |
| `ready_to_task` | 1243 | **1336** | 2306 |

A separate scheduler/PendSV diagnostic on the same pre-fix behavior measured:

| Segment | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| PendSV save | 61 | **65** | 70 |
| scheduler decision | 295 | **331** | 349 |
| PendSV restore | 10 | **10** | 10 |
| PendSV software | 399 | **430** | 450 |
| PendSV to task continuation | 1294 | **1359** | 1373 |

Derived average intervals:

```text
non-scheduler PendSV software: 24 cycles
post-PendSV-software return/API interval: 929 cycles
```

The important observation was that the complete measured scheduler decision was only about 331 cycles, while 929 cycles remained after the diagnostic PendSV software interval. The scheduler could therefore not account for the dominant regression.

## Root cause

Core task-context blocking/yield paths use a two-stage port contract:

```c
hrt__pend_context_switch();
hrt_port_yield_to_scheduler();
```

This contract is meaningful on the POSIX port:

```text
hrt__pend_context_switch()    -> record that a reschedule is pending
hrt_port_yield_to_scheduler() -> perform the host context hop
```

The Cortex-M port incorrectly implemented **both stages** by calling the same `_pend_pendsv()` helper. `_pend_pendsv()` writes `PENDSVSET` and executes the Cortex-M barriers needed to expose the request.

Consequently a task-context blocking path could request PendSV once through `hrt__pend_context_switch()` and request it again when execution continued through `hrt_port_yield_to_scheduler()`. The second hardware request was redundant and could cause an unnecessary additional scheduler/context-switch pass before the blocking API returned.

The redundant path was old, but the scheduler-correctness work made a scheduler pass more expensive because it correctly preserves outgoing-task queue precedence and remaining RR quantum. That amplified a latent Cortex-M port inefficiency into an obvious response-time regression.

Fix:

```text
12f745673f8ce5069903eab314b38e43591b0899
perf(hardrt): avoid duplicate Cortex-M PendSV request
```

After the fix:

```text
hrt__pend_context_switch()    -> requests PendSV
hrt_port_yield_to_scheduler() -> no second hardware request on Cortex-M
```

The POSIX implementation is unchanged.

## After the Cortex-M port fix

Two clean development runs at `12f74567` provide the post-fix evidence:

```text
Scheduler diagnostic run: 20260903T202444Z_12f74567
Functional run:           20260903T202544Z_12f74567
HardRT: 12f745673f8ce5069903eab314b38e43591b0899
Tracked source: clean
STM32CubeH7: f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
Samples: 10000
Functional hardware matrix: 13/13 PASS
Combined development archive SHA-256:
cf928934476f1f74284acbcd508bd764a070ed986cb491c4ba5d09c484d6e52b
```

Production-path measurements:

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` | 1133 | **1183** | 1508 |
| `sem_isr_ready` | 319 | **334** | 339 |
| `ready_to_task` | 692 | **780** | 1764 |

Scheduler/PendSV diagnostic:

| Segment | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| PendSV save | 61 | **65** | 70 |
| scheduler decision | 309 | **329** | 349 |
| PendSV restore | 10 | **10** | 10 |
| PendSV software | 404 | **429** | 444 |
| PendSV to task continuation | 659 | **680** | 693 |

Derived average intervals:

```text
non-scheduler PendSV software: 25 cycles
post-PendSV-software return/API interval: 251 cycles
```

## A/B result

The port fix changed almost nothing inside the measured scheduler/PendSV software body, but removed almost the entire excess tail:

| Metric | Before avg | After avg | Delta |
|---|---:|---:|---:|
| `event_to_task` | 1705 | **1183** | **-522 cycles (-30.6%)** |
| `sem_isr_ready` | 328 | **334** | +6 cycles (+1.8%) |
| `ready_to_task` | 1336 | **780** | **-556 cycles (-41.6%)** |
| scheduler decision | 331 | **329** | -2 cycles (-0.6%) |
| PendSV software | 430 | **429** | -1 cycle (-0.2%) |
| PendSV to task | 1359 | **680** | **-679 cycles (-50.0%)** |
| derived return/API tail | 929 | **251** | **-678 cycles (-73.0%)** |

This is strong evidence for the diagnosed root cause:

- semaphore/READY work did not become materially faster;
- scheduler-decision time did not become materially faster;
- PendSV software time did not become materially faster;
- the response interval after the first PendSV software work collapsed by roughly the exact amount needed to explain the composite latency recovery;
- all 13 functional Cortex-M contracts still passed after the change.

The correction therefore recovered performance **without reverting scheduler correctness**.

## Interpretation of the remaining cost

The remaining measured costs are not automatically regressions.

At 64 MHz, the post-fix average scheduler decision of 329 cycles is about 5.14 microseconds. On the measured asynchronous priority-preemption path it includes the required scheduler semantics:

1. save/update the outgoing task stack pointer;
2. classify the outgoing READY task as blocked/yielded/quantum-expired/asynchronously preempted;
3. preserve queue precedence and remaining RR quantum when required;
4. reinsert the outgoing task at the correct front/tail position;
5. select the highest READY priority from the bitmap;
6. pop the next task from its ready queue;
7. update the current-task identity;
8. load the incoming stack pointer.

Those operations are semantically required by the current scheduler contract. Some implementation overhead may still be reducible, but the measurement does not justify deleting or weakening those semantics.

Likewise, the diagnostic `pendsv_to_task - pendsv_software` remainder is not pure production exception-return cost: the diagnostic handler performs additional DWT/result stores after the `pendsv_software` timestamp. The 251-cycle derived value must therefore not be treated as a 251-cycle production Cortex-M exception-return penalty.

The correct next step for further scheduler optimization is generated-code/disassembly analysis and targeted measurement of scheduler sub-operations. Do not infer that every remaining cycle is waste simply because the earlier architectural bug was waste.

---

# v0.5 development: tick/sleeper scaling baseline

The ready side of the scheduler had already been converted to intrusive per-priority FIFOs plus a non-empty priority bitmap. The remaining scheduler-structure question in #57 was whether the simple full-TCB sleeper scan was acceptable at the task counts HardRT intends to support.

A dedicated hardware matrix was added before changing the implementation so the decision would be based on physical measurements and the same fixture could later provide a direct A/B comparison.

## Pre-delta hardware baseline

Full development run:

```text
Run ID: 20260903T212538Z_4ab7709a
HardRT: 4ab7709ab6a3657214a6786ebbd2bc315716907c
Tracked source: clean
STM32CubeH7: f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
Core: STM32H755 CM7 at 64 MHz
Tick: external TIM2, logical 1 kHz
Samples: 10000 per benchmark image
Functional contracts: 9/9 PASS
Hardware benchmarks: 22/22 PASS
Evidence archive SHA-256:
6f32cf6ce1fb9db1955c65670bb759e3ee1cd7a7f5d83bbb383110173c2bd56d
```

The first four benchmark images also reproduced the post-PendSV-fix latency baseline:

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` | 1133 | **1183** | 1508 |
| `sem_isr_ready` | 319 | **334** | 339 |
| `ready_to_task` | 692 | **780** | 1764 |
| `scheduler_decision` | 309 | **329** | 349 |

This confirms the recovered context-switch latency was stable while the sleeper investigation was performed.

## Tick/sleeper matrix

The old implementation scanned all configured TCB slots in `hrt__tick_isr()` every tick. The benchmark rebuilt HardRT at application-task capacities 8, 16, and 32 and measured six workload shapes. Each value below is the average DWT cycles for the production `hrt_tick_from_isr()` call.

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 902 | 1050 | 1124 | 1367 | 2282 | 1700 |
| 16 | 1389 | 1506 | 1761 | 1808 | 3990 | 2435 |
| 32 | 1954 | 2597 | 3455 | 2952 | 8676 | 4413 |

At 32 application tasks and 64 MHz:

| Workload | Avg cycles | Avg us | Fraction of a 1 ms tick |
|---|---:|---:|---:|
| no sleepers | 1954 | 30.5 | 3.05% |
| all sleeping, no expiry | 3455 | 54.0 | 5.40% |
| one expiry | 2952 | 46.1 | 4.61% |
| staggered | 4413 | 69.0 | 6.90% |
| 31 simultaneous expiries | 8676 | 135.6 | 13.56% |

The 32-task staggered measured maximum was 7227 cycles, about 112.9 us or 11.29% of the 1 ms period.

## Workload sanity proof

The benchmark uses a one-shot timer handshake so each measured tick starts only after the workers awakened by the previous sample have run and returned to their intended sleep/block state. Worker-completion counters matched the workload definitions exactly:

```text
one_expiry: 9999 wakes for each capacity

simultaneous:
7  * 9999 =  69993
15 * 9999 = 149985
31 * 9999 = 309969

staggered harmonic totals:
8-task build:  25923
16-task build: 33173
32-task build: 40254
```

The final sample intentionally stops before the tasks awakened by that final IRQ can complete, hence 9,999 rather than 10,000 repeated wake completions.

These counters make the matrix more than a set of plausible timing values: the intended expiry populations actually occurred.

## Separating scan work from actual wake work

The simultaneous-expiry case includes legitimate work proportional to the number of tasks made READY. Subtracting the same-capacity `all_sleep` case provides a rough estimate of the incremental wake/requeue cost:

```text
8 tasks:  (2282 - 1124) / 7  ~= 165 cycles per wake
16 tasks: (3990 - 1761) / 15 ~= 149 cycles per wake
32 tasks: (8676 - 3455) / 31 ~= 168 cycles per wake
```

The consistency suggests roughly 150-170 cycles per actual wake is expected work for this configuration.

The stronger reason to replace the scan is therefore the **no-expiry** cost. At 32 application tasks the old implementation spent 1954 cycles with no sleepers and 3455 cycles with 31 sleepers whose deadlines were not due. That work recurred on every tick even though no task needed to wake.

## Selected replacement

The physical measurements justify a static intrusive delta sleeper queue:

```text
hrt_sleep() insertion: bounded O(N) in task context
no-expiry tick:        O(1)
K expiries:            O(K)
metadata:               O(N), static
allocation:             none
```

Equal deadlines are represented by zero-delta followers and preserve insertion/FIFO wake order. Relative deltas avoid ordering sleepers by wrapped absolute tick values.

The implementation is covered by hosted tests for equal deadlines, staggered deadlines, repeated one-tick sleep/wake cycles, and multi-sleeper ordering across 32-bit tick wrap. CI #219 passed 57/57 hosted tests and the STM32H755 cross-build.

**Post-change physical performance remains pending.** The same 8/16/32 by six-workload benchmark matrix must be rerun before claiming an achieved cycle reduction. The expected structural signature is that `none`, `one_sleep`, and `all_sleep` stop scaling materially with configured task capacity, while scenarios with actual expiries retain cost proportional to the number of tasks that genuinely wake.

---

# Historical v0.4.0 measurements

The following results were recorded for HardRT v0.4.0 on an STM32H755 Cortex-M7 at 64 MHz. The historical record did not pin every compiler, linker, cache, flash-wait-state, FPU, interrupt-load, and memory-placement detail required for a formal bound, so these remain comparison observations.

### Historical measurement method

- ISR timestamps event using `DWT->CYCCNT`.
- ISR signals a semaphore with `hrt_sem_give_from_isr()`.
- awakened task timestamps after semaphore take.
- 10,000 samples per case.

Representative equal-priority result:

```text
SystemCoreClock=64000000 Hz

[TICK -> TASK]
count=10000
min=1161 cycles, avg=1414 cycles, max=2232 cycles

[SEM GIVE -> TASK TAKE]
count=10000
min=1201 cycles, avg=1547 cycles, max=2893 cycles
```

Priority-interaction results:

| Test | Task priorities | Metric | Min cycles | Avg cycles | Max cycles | Min us | Avg us | Max us |
|---:|---|---|---:|---:|---:|---:|---:|---:|
| 0 | Tick PRIO0, Event PRIO0 | Tick to task | 1161 | 1414 | 2232 | 18 | 22 | 34 |
| 0 | Tick PRIO0, Event PRIO0 | Event to task | 1201 | 1547 | 2893 | 18 | 24 | 45 |
| 1 | Tick PRIO0, Event PRIO1 | Tick to task | 1161 | 1348 | 1879 | 18 | 21 | 29 |
| 1 | Tick PRIO0, Event PRIO1 | Event to task | 1301 | 1636 | 2957 | 20 | 25 | 46 |
| 2 | Tick PRIO1, Event PRIO0 | Tick to task | 1261 | 1741 | 3559 | 19 | 27 | 55 |
| 2 | Tick PRIO1, Event PRIO0 | Event to task | 1158 | 1221 | 1242 | 18 | 19 | 19 |

Microsecond values are approximate conversions from the reported 64 MHz cycle counts.

## General interpretation limits

Observed latency depends on workload and configuration. Relevant factors include:

- HardRT critical-section duration;
- higher-urgency interrupts above the `BASEPRI` ceiling;
- already-ready higher-priority tasks;
- synchronization/waiter processing;
- scheduler policy and queue state;
- compiler optimization and code placement;
- cache, flash, bus, and memory effects;
- debug/trace instrumentation;
- optional floating-point context requirements.

A continuously ready higher-priority task can starve lower-priority work. Application deadline claims require workload and interference analysis in addition to kernel measurements.

A measured maximum is not a worst-case execution-time proof. Use these numbers as reproducible engineering observations for the recorded setup, not as unconditional latency guarantees.
