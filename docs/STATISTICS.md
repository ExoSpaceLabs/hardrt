# HardRT Timing and Latency Measurements

This document records STM32H755 Cortex-M7 engineering measurements. Values characterize the stated configuration/workload; they are **not universal WCET bounds**.

## Common development target

- MCU/core: STM32H755 / Cortex-M7
- Board: NUCLEO-H755ZI-Q
- Core clock in recorded development runs: 64 MHz
- HardRT port: `cortex_m`
- Build: Release
- Semihosting: disabled
- Typical sample count: 10,000 per benchmark image

Formal hard-real-time claims require additional compile/link, cache/FPU, memory-placement, board-revision, IRQ/interference, and workload assumptions.

## Historical scheduler/lifecycle development baseline

Run `20260905T134123Z_80f2042f` on SHA `80f2042f2c64053a9ea888666474c5dad5f72797` passed 11/11 functional contracts and the historical 22-image benchmark suite.

Representative wake/switch results:

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` | 1407 | 1457 | 2001 |
| `sem_isr_ready` | 300 | 319 | 327 |
| `ready_to_task` | 922 | 1001 | 1597 |
| `scheduler_decision` | 365 | 380 | 411 |

`event_to_task` is a **legacy semaphore-backed composite measurement** whose name predates the v0.5 event-flags API. It must not be interpreted as event-flag primitive timing.

Scheduler/PendSV decomposition from that run:

```text
pendsv_save=88 / 88 / 88 cycles
scheduler_decision=365 / 380 / 411 cycles
pendsv_restore=63 / 63 / 63 cycles
pendsv_software=562 / 577 / 608 cycles
pendsv_to_task=779 / 800 / 1365 cycles
```

## Event/notification development baseline

Physical feature validation later passed all 13 v0.5 functional contracts plus the historical 22 benchmarks. The final runner subsequently integrated a dedicated **16-image event/notification timing matrix**.

The signal metrics are:

- `event_isr_to_task`: event ISR producer entry to awakened event waiter continuation;
- `notify_isr_to_task`: notification ISR producer entry to awakened notification waiter continuation;
- `event_scan_none`: event ISR set cost with zero matching waiters;
- `event_scan_one`: event ISR set cost with exactly one matching waiter;
- `event_scan_all`: event ISR set cost with every registered waiter matching;
- `notify_isr_no_wake`: notification ISR producer cost without READY transition;
- `notify_isr_wake`: notification ISR producer cost when a blocked target becomes READY.

`event_scan_none`, `event_scan_one`, and `event_scan_all` are each measured with **1, 8, 16, and 32 actual registered waiters**. Firmware validates expected/observed wake fan-out so the measured workload cannot silently collapse into a cheaper path.

The production event/notification code remains uninstrumented internally; the profiling images use direct application-side DWT timestamps with `HARDRT_TIMING_PROFILE=none`.

**Final v0.5.0 signal numbers are intentionally not hard-coded into this source file before qualification.** They belong to the selected hardware qualification artifact generated from the frozen release SHA. This avoids modifying the source tree after the physical run merely to copy measured values into documentation and thereby invalidating the qualified SHA.

## Tick/sleeper scaling

Current v0.5 uses a static intrusive delta sleeper queue:

```text
hrt_sleep() insertion: bounded O(N) in task context
no-expiry tick:        O(1)
K expiries:            O(K) plus READY publication
metadata:               O(N), static
allocation:             none
```

Average cycles from the accepted `80f2042f` development run:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 543 | 544 | 592 | 830 | 1893 | 1171 |
| 16 | 543 | 544 | 592 | 830 | 3181 | 1290 |
| 32 | 504 | 552 | 555 | 837 | 5249 | 1331 |

The structural result is more important than a particular average: no-expiry cases remain essentially independent of configured task capacity, while actual expiry work scales with tasks that truly wake.

## Earlier full-scan comparison

Before the intrusive sleeper queue, average cycles at 8/16/32 configured application tasks were:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 902 | 1050 | 1124 | 1367 | 2282 | 1700 |
| 16 | 1389 | 1506 | 1761 | 1808 | 3990 | 2435 |
| 32 | 1954 | 2597 | 3455 | 2952 | 8676 | 4413 |

The former recurring O(N) no-expiry scan justified the current delta-queue design.

## Scheduler latency correction history

During v0.5 hardening, Cortex-M task-context blocking/yield paths were found to request PendSV redundantly. Commit `12f745673f8ce5069903eab314b38e43591b0899` (`perf(hardrt): avoid duplicate Cortex-M PendSV request`) removed that duplicate hardware request while preserving scheduler semantics.

Representative average values before/after that correction:

| Metric | Before avg | After avg |
|---|---:|---:|
| legacy `event_to_task` | 1705 | 1183 |
| `sem_isr_ready` | 328 | 334 |
| `ready_to_task` | 1336 | 780 |
| scheduler decision | 331 | 329 |
| PendSV software | 430 | 429 |
| PendSV to task | 1359 | 680 |

## Historical v0.4.0 observations

Representative v0.4.0 H755 measurements at 64 MHz are retained only for historical comparison:

| Test | Task priorities | Metric | Min | Avg | Max |
|---:|---|---|---:|---:|---:|
| 0 | Tick P0, Event P0 | Tick to task | 1161 | 1414 | 2232 |
| 0 | Tick P0, Event P0 | Event to task | 1201 | 1547 | 2893 |
| 1 | Tick P0, Event P1 | Tick to task | 1161 | 1348 | 1879 |
| 1 | Tick P0, Event P1 | Event to task | 1301 | 1636 | 2957 |
| 2 | Tick P1, Event P0 | Tick to task | 1261 | 1741 | 3559 |
| 2 | Tick P1, Event P0 | Event to task | 1158 | 1221 | 1242 |

## Interpretation limits

Observed latency depends on critical-section duration, interrupt interference, already-ready higher-priority tasks, scheduler policy, waiter population, queue payload-copy size, compiler/placement/cache effects, and floating-point context requirements.

The v0.5 hardware suite provides reproducible engineering evidence for its recorded configuration. Analytical critical-section bounds, bounded mutex priority inversion, queue-copy scaling, richer interference analysis, and fully machine-readable qualification metadata remain 1.0-quality work.

A measured maximum is not a WCET proof.
