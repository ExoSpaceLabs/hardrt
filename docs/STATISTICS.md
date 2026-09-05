# HardRT Timing and Latency Measurements

This document records hardware measurements for HardRT on STM32H755 Cortex-M7. The values characterize the stated configuration and workload. They are **measured observations**, not universal WCET or end-to-end hard-real-time bounds.

## Common target

- MCU: STM32H755, Cortex-M7 core
- Board: NUCLEO-H755ZI-Q
- Core clock: 64 MHz (`SystemCoreClock = 64_000_000`)
- HardRT port: `cortex_m`
- Build: Release
- Semihosting: disabled
- Timing sample count: 10,000 per benchmark image

For formal hard-real-time claims, additional assumptions such as complete compile/link flags, cache/FPU/flash placement, interrupt load and priorities, board revision, memory placement and external interference must also be recorded.

---

# Current accepted v0.5 development baseline

The scheduler/lifecycle hardening phase is accepted on physical hardware at:

```text
Run ID:       20260905T134123Z_80f2042f
HardRT:       80f2042f2c64053a9ea888666474c5dad5f72797
Branch:       develop
Tracked tree: clean
STM32CubeH7:  f5c0b7a2b1f6eb26fde150f72edb2d7deb647066 / clean
ARM GCC:      10.3.1
GDB:          12.1
OpenOCD:      0.11.0
CMake:        3.22.1
```

Qualification result:

```text
Board probe:           PASS
Functional contracts:  11 / 11 PASS
Hardware benchmarks:   22 / 22 PASS
Overall:                PASS
```

This is accepted development evidence. The final v0.5.0 release package must be regenerated from the exact frozen RC SHA after all release-facing work is complete.

## Current wake/switch measurements

| Metric | Min cycles | Avg cycles | Max cycles |
|---|---:|---:|---:|
| `event_to_task` | 1407 | **1457** | 2001 |
| `sem_isr_ready` | 300 | **319** | 327 |
| `ready_to_task` | 922 | **1001** | 1597 |
| `scheduler_decision` | 365 | **380** | 411 |

The metric meanings are:

- `event_to_task`: software timestamp inside the TIM2 ISR before the semaphore wake path to continuation of the awakened task after the blocked semaphore API returns; this is **not** hardware interrupt-entry latency;
- `sem_isr_ready`: ISR semaphore-call entry to waiter READY publication;
- `ready_to_task`: waiter READY publication to continuation after the blocked API returns;
- `scheduler_decision`: diagnostic execution of the production scheduler decision path.

## Current scheduler/PendSV decomposition

```text
pendsv_save=88 / 88 / 88 cycles (min/avg/max)
scheduler_decision=365 / 380 / 411 cycles
pendsv_restore=63 / 63 / 63 cycles
pendsv_software=562 / 577 / 608 cycles
pendsv_to_task=779 / 800 / 1365 cycles
derived_software_other_avg=46 cycles
derived_return_and_api_avg=223 cycles
```

The scheduler diagnostic uses measurement instrumentation around the switch path. Derived values are engineering decompositions, not WCET bounds or exact production instruction counts.

---

# Tick/sleeper scaling

## Former full-TCB scan baseline

Before the intrusive delta sleeper queue, the production tick path scanned configured task storage even when no sleeper expired.

Development run `20260903T212538Z_4ab7709a` at 64 MHz and 10,000 samples measured these average cycles:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 902 | 1050 | 1124 | 1367 | 2282 | 1700 |
| 16 | 1389 | 1506 | 1761 | 1808 | 3990 | 2435 |
| 32 | 1954 | 2597 | 3455 | 2952 | 8676 | 4413 |

At 32 application tasks, even the no-sleeper case cost 1954 cycles and the all-sleep/no-expiry case cost 3455 cycles every tick. That recurring O(N) work justified replacing the scan.

## Current intrusive delta queue

Current `develop` uses:

```text
hrt_sleep() insertion: bounded O(N) in task context
no-expiry tick:        O(1)
K expiries:            O(K)
metadata:               O(N), static
allocation:             none
```

Equal deadlines use zero-delta followers and preserve deterministic FIFO wake order. Relative deltas avoid sorting sleepers by wrapped absolute tick values.

The accepted `80f2042f` physical run measured these average cycles:

| app tasks | none | one_sleep | all_sleep | one_expiry | simultaneous | staggered |
|---:|---:|---:|---:|---:|---:|---:|
| 8 | 543 | 544 | 592 | 830 | 1893 | 1171 |
| 16 | 543 | 544 | 592 | 830 | 3181 | 1290 |
| 32 | 504 | 552 | 555 | 837 | 5249 | 1331 |

The structural result is the important part: `none`, `one_sleep`, and `all_sleep` no longer scale materially with configured application-task capacity, while actual expiry work scales with the number of tasks that wake.

At 32 application tasks, compared with the former full scan:

| Workload | Old avg | Current avg | Change |
|---|---:|---:|---:|
| none | 1954 | 504 | -74.2% |
| one_sleep | 2597 | 552 | -78.7% |
| all_sleep | 3455 | 555 | -83.9% |
| one_expiry | 2952 | 837 | -71.6% |
| simultaneous | 8676 | 5249 | -39.5% |
| staggered | 4413 | 1331 | -69.8% |

The simultaneous case still performs legitimate READY publication for 31 workers and therefore remains O(K), as intended.

---

# Scheduler latency regression and correction history

During v0.5 scheduler hardening, a correctness update exposed a pre-existing Cortex-M inefficiency: task-context blocking/yield paths could request PendSV twice because both `hrt__pend_context_switch()` and `hrt_port_yield_to_scheduler()` mapped to the same hardware PendSV request.

The fix was:

```text
12f745673f8ce5069903eab314b38e43591b0899
perf(hardrt): avoid duplicate Cortex-M PendSV request
```

Afterward, Cortex-M keeps one hardware PendSV request while POSIX retains its separate hosted context-hop behavior. A/B hardware measurements showed the response tail collapse without weakening scheduler semantics.

Representative averages before versus immediately after that fix were:

| Metric | Before avg | After avg |
|---|---:|---:|
| `event_to_task` | 1705 | 1183 |
| `sem_isr_ready` | 328 | 334 |
| `ready_to_task` | 1336 | 780 |
| scheduler decision | 331 | 329 |
| PendSV software | 430 | 429 |
| PendSV to task | 1359 | 680 |

The scheduler itself was not the source of the dominant regression; the redundant second PendSV request was.

---

# Historical v0.4.0 measurements

Historical HardRT v0.4.0 measurements on STM32H755 CM7 at 64 MHz remain useful as comparison observations, but the historical record did not pin every configuration detail needed for a formal bound.

Representative measurements:

| Test | Task priorities | Metric | Min cycles | Avg cycles | Max cycles |
|---:|---|---|---:|---:|---:|
| 0 | Tick PRIO0, Event PRIO0 | Tick to task | 1161 | 1414 | 2232 |
| 0 | Tick PRIO0, Event PRIO0 | Event to task | 1201 | 1547 | 2893 |
| 1 | Tick PRIO0, Event PRIO1 | Tick to task | 1161 | 1348 | 1879 |
| 1 | Tick PRIO0, Event PRIO1 | Event to task | 1301 | 1636 | 2957 |
| 2 | Tick PRIO1, Event PRIO0 | Tick to task | 1261 | 1741 | 3559 |
| 2 | Tick PRIO1, Event PRIO0 | Event to task | 1158 | 1221 | 1242 |

---

# Interpretation limits and remaining qualification work

Observed latency depends on workload and configuration. Relevant factors include:

- HardRT critical-section duration;
- higher-urgency interrupts above the `BASEPRI` ceiling;
- already-ready higher-priority tasks;
- synchronization/waiter processing;
- scheduler policy and queue state;
- queue item-copy size;
- compiler optimization and code placement;
- cache, flash, bus, and memory effects;
- debug/trace instrumentation;
- floating-point context requirements.

The v0.5 scheduler/lifecycle hardening is considered complete at the development level because the exact tested code passes the full functional and benchmark suite. Remaining broader hard-real-time work is deliberately **not a v0.5 scheduler-hardening blocker** and includes maximum critical-section characterization, priority-inversion mitigation, queue-copy scaling, interference testing, complete machine-readable timing output and all metadata needed for configuration-specific upper-bound claims.

A measured maximum is not a worst-case execution-time proof. Use these numbers as reproducible engineering observations for the recorded setup, not as unconditional latency guarantees.
