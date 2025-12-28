# HardRT v0.3.0 – Timing & Latency Characterization

This document summarizes **event → task latency measurements** for HardRT v0.3.0 on Cortex-M, with a focus on determinism and priority behavior rather than absolute best-case numbers.

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
Breakpoint 1 at 0x8000cfc
Note: automatically using hardware breakpoints for read-only addresses.
Breakpoint 2 at 0x80007c6

--- Target reached ---
SystemCoreClock=64000000 Hz

[TICK -> TASK]
count=10000
min=1884 cycles, avg=2141 cycles (sum/count=2141), max=3121 cycles
min=29 us, avg=33 us, max=48 us (approx)
min=29437 ns, avg=33453 ns, max=48765 ns

[SEM GIVE -> TASK TAKE]
count=10000
min=1921 cycles, avg=2332 cycles (sum/count=2332), max=4008 cycles
min=30 us, avg=36 us, max=62 us (approx)
min=30015 ns, avg=36437 ns, max=62625 ns

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
|    0 | Tick **PRIO0**, Event **PRIO0** | Tick → Task  |         1164 |         1411 |         2138 |       18 |       22 |       33 |
|    0 | Tick **PRIO0**, Event **PRIO0** | Event → Task |         1190 |         1549 |         2910 |       18 |       24 |       45 |
|    1 | Tick **PRIO0**, Event **PRIO1** | Tick → Task  |         1164 |         1342 |         1824 |       18 |       20 |       28 |
|    1 | Tick **PRIO0**, Event **PRIO1** | Event → Task |         1304 |         1617 |         2950 |       20 |       25 |       46 |
|    2 | Tick **PRIO1**, Event **PRIO0** | Tick → Task  |         1258 |         1729 |         3485 |       19 |       27 |       54 |
|    2 | Tick **PRIO1**, Event **PRIO0** | Event → Task |         1208 |         1255 |         1313 |       18 |       19 |       20 |

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

---

## Notes on Tick Source

These measurements were performed using **external tick mode**.  
This choice isolates **event → task latency** from kernel timekeeping behavior.

Using SysTick instead would primarily affect:
- tick ISR execution time
- wake-up of sleeping tasks

It does **not** materially change the semaphore-based event → task latency path shown above.
