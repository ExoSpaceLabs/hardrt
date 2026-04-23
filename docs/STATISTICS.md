# HardRT – Timing & Latency Characterization

This document contains performance data collected for **HardRT v0.4.0** on STM32H7.
The measurements demonstrate deterministic behavior and the impact of task priorities on scheduling latency.

All measurements were performed **without modifying HardRT core logic**, using application-level instrumentation and the Cortex-M DWT cycle counter.

---

## Test Setup

**Target**
- MCU: STM32H755 (Cortex-M7)
- Core clock: 64 MHz (`SystemCoreClock = 64_000_000`)
- Port: `cortex_m`

**Build**
- Configuration: Release
- Semihosting: Disabled
- Debug variables: Disabled

**Measurement method**
- ISR timestamps event using `DWT->CYCCNT`
- ISR signals a semaphore (`hrt_sem_give_from_isr`)
- High-priority task timestamps on semaphore take
- Latency = `t_task_entry - t_event`
- Samples per test: 10,000

**Tick mode**
- External tick (kernel time advanced externally)
- Measurements focus on **event → task execution**, not timekeeping accuracy

---

## Representative Run (Tick PRIO0, Event PRIO0)

This run shows the full console output for the **equal-priority** case (both tick and event tasks at **PRIO0**).

Command used:
```
dev in dev in hardrt on  develop [!?⇡] via △ v3.22.1 took 25s 
➜ gdb-multiarch -q examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf -batch -x scripts/gdb/timing.dbg
```

Console output:
```
0x080006cc in Reset_Handler ()
semihosting is disabled

target halted due to debug-request, current mode: Thread 
xPSR: 0x01000000 pc: 0x080006cc msp: 0x20020000
Breakpoint 1 at 0x8000d68
Note: automatically using hardware breakpoints for read-only addresses.
Breakpoint 2 at 0x80007c6

--- Target reached ---
SystemCoreClock=64000000 Hz

[TICK -> TASK]
count=10000
min=1161 cycles, avg=1414 cycles (sum/count=1414), max=2232 cycles
min=18 us, avg=22 us, max=34 us (approx)
min=18140 ns, avg=22093 ns, max=34875 ns

[SEM GIVE -> TASK TAKE]
count=10000
min=1201 cycles, avg=1547 cycles (sum/count=1547), max=2893 cycles
min=18 us, avg=24 us, max=45 us (approx)
min=18765 ns, avg=24171 ns, max=45203 ns

[TIMER CFG]
TIM2: PSC=31 ARR=999 us period
TIM3: PSC=31 ARR=4999 us period
[Inferior 1 (Remote target) detached]
```

---

## Priority Interaction Results

Two latency-sensitive tasks were used:
- **Tick task** (periodic event)
- **Event task** (asynchronous external event)

Both tasks are woken via ISR-signaled semaphores.

### Summary Table

| Test | Task Priorities                 | Metric       | Min (cycles) | Avg (cycles) | Max (cycles) | Min (µs) | Avg (µs) | Max (µs) |
|-----:|---------------------------------|--------------|-------------:|-------------:|-------------:|---------:|---------:|---------:|
|    0 | Tick **PRIO0**, Event **PRIO0** | Tick → Task  |         1161 |         1414 |         2232 |       18 |       22 |       34 |
|    0 | Tick **PRIO0**, Event **PRIO0** | Event → Task |         1201 |         1547 |         2893 |       18 |       24 |       45 |
|    1 | Tick **PRIO0**, Event **PRIO1** | Tick → Task  |         1161 |         1348 |         1879 |       18 |       21 |       29 |
|    1 | Tick **PRIO0**, Event **PRIO1** | Event → Task |         1301 |         1636 |         2957 |       20 |       25 |       46 |
|    2 | Tick **PRIO1**, Event **PRIO0** | Tick → Task  |         1261 |         1741 |         3559 |       19 |       27 |       55 |
|    2 | Tick **PRIO1**, Event **PRIO0** | Event → Task |         1158 |         1221 |         1242 |       18 |       19 |       19 |

---

## Interpretation

- When multiple tasks are woken concurrently, **HardRT always schedules the highest-priority READY task first**.
- With equal priority (PRIO0/PRIO0), both latency paths remain in the same ballpark, but jitter reflects contention and timing alignment.
- The highest-priority task consistently exhibits **lower average latency and tighter jitter** when priorities differ.
- Lower-priority tasks absorb interference caused by:
    - context save/restore of higher-priority tasks
    - scheduler execution
    - shared interrupt and memory activity

This behavior is **intentional and deterministic**, and matches the expected contract of a priority-based real-time scheduler.

**Note on v0.4.0 changes:** 
Measurements for v0.4.0 show a slight improvement in the best-case latency (min cycles) and more consistent performance across priority levels compared to v0.3.0. The fundamental behavior remains identical: higher priority tasks pre-empt or execute before lower priority tasks when both are ready.

---

## Notes on Tick Source

These measurements were performed using **external tick mode**.  
This choice isolates **event → task latency** from kernel timekeeping behavior.

Using SysTick instead would primarily affect:
- tick ISR execution time
- wake-up of sleeping tasks

It does **not** materially change the semaphore-based event → task latency path shown above.
