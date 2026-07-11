# HardRT Timing and Latency Measurements

This document records measurements collected for HardRT v0.4.0 on one STM32H755 Cortex-M7 setup. The results characterize that setup and workload. They are not a proof of a universal worst-case execution-time or end-to-end latency bound.

## Recorded setup

### Target

- MCU: STM32H755, Cortex-M7 core
- Core clock: 64 MHz (`SystemCoreClock = 64_000_000`)
- HardRT port: `cortex_m`

### Build

- Configuration: Release
- Semihosting: disabled
- HardRT debug variables: disabled

The original record does not pin every compiler, linker, cache, flash-wait-state, FPU, interrupt-load, and memory-placement detail needed to reproduce a formal upper bound. Treat the values below as historical measurements until that information and the raw artifacts are captured together.

### Measurement method

- An ISR timestamps an event using `DWT->CYCCNT`.
- The ISR calls `hrt_sem_give_from_isr()`.
- The awakened task timestamps after its semaphore take returns.
- Reported latency is the unsigned cycle difference between those timestamps.
- Each recorded case contains 10,000 samples.

### Tick mode

The measurements used external tick mode. They focus on ISR signal to task execution rather than accuracy of the kernel time base.

## Representative recorded run

The following command shape was used with the repository timing example and GDB script:

```bash
gdb-multiarch \
  -q examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf \
  -batch \
  -x scripts/gdb/timing.dbg
```

The build directory is illustrative. Reproduction requires generating the firmware for the same target and configuration first.

Recorded output for the equal-priority case:

```text
SystemCoreClock=64000000 Hz

[TICK -> TASK]
count=10000
min=1161 cycles, avg=1414 cycles, max=2232 cycles
min=18 us, avg=22 us, max=34 us (approx)

[SEM GIVE -> TASK TAKE]
count=10000
min=1201 cycles, avg=1547 cycles, max=2893 cycles
min=18 us, avg=24 us, max=45 us (approx)

[TIMER CFG]
TIM2: PSC=31 ARR=999 us period
TIM3: PSC=31 ARR=4999 us period
```

## Recorded priority-interaction results

Two tasks were used:

- a periodic tick-related task;
- an asynchronous event task.

Both were awakened through ISR semaphore gives.

| Test | Task priorities | Metric | Min cycles | Avg cycles | Max cycles | Min µs | Avg µs | Max µs |
|---:|---|---|---:|---:|---:|---:|---:|---:|
| 0 | Tick PRIO0, Event PRIO0 | Tick to task | 1161 | 1414 | 2232 | 18 | 22 | 34 |
| 0 | Tick PRIO0, Event PRIO0 | Event to task | 1201 | 1547 | 2893 | 18 | 24 | 45 |
| 1 | Tick PRIO0, Event PRIO1 | Tick to task | 1161 | 1348 | 1879 | 18 | 21 | 29 |
| 1 | Tick PRIO0, Event PRIO1 | Event to task | 1301 | 1636 | 2957 | 20 | 25 | 46 |
| 2 | Tick PRIO1, Event PRIO0 | Tick to task | 1261 | 1741 | 3559 | 19 | 27 | 55 |
| 2 | Tick PRIO1, Event PRIO0 | Event to task | 1158 | 1221 | 1242 | 18 | 19 | 19 |

Microsecond values are approximate conversions from the reported 64 MHz cycle counts.

## Interpretation limited to this run

The recorded traces are consistent with the current priority-ordered ready-queue implementation: when both measured tasks were ready at a scheduling decision, the higher-priority task was selected first.

The measurements also show that observed latency varied with task priority and contention in this workload. They do not establish that every higher-priority wake has the same latency, or that lower-priority delay is bounded independently of application behavior.

Factors not represented by the simplified table can include:

- time spent in HardRT critical sections;
- higher-urgency interrupts permitted above the `BASEPRI` ceiling;
- execution of already-ready higher-priority tasks;
- queue or semaphore processing;
- compiler optimization and code placement;
- cache, flash, bus, and memory effects;
- debug or trace instrumentation;
- optional floating-point context requirements.

A continuously ready higher-priority task can starve lower-priority work. Any application-level deadline claim therefore requires a workload and interrupt analysis in addition to the kernel measurements.

## Tick-source relevance

External tick mode was used to isolate the semaphore event path from SysTick configuration. Changing to the internal SysTick source can change interrupt interaction, tick-handler timing, and wake-up timing. The data here should not be assumed unchanged without measurement.

## Reproduction status

The repository contains the timing example and GDB script referenced above, but the historical record does not yet contain a complete reproducibility bundle with:

- exact HardRT commit SHA;
- compiler and binutils versions;
- complete compile and link flags;
- board revision and clock configuration;
- cache, FPU, and flash settings;
- interrupt priorities and competing interrupt load;
- raw machine-readable samples;
- analysis script and expected output.

Until those are recorded together, use this page as a performance observation for the stated setup, not as a certified maximum-latency specification.