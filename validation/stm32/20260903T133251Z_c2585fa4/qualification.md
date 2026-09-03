# HardRT STM32H755 Qualification Report

- Run ID: `20260903T133251Z_c2585fa4`
- UTC start: `2026-09-03T13:33:03+00:00`
- Local start: `2026-09-03T15:33:03+02:00`
- Tester: `dev`
- Board: `NUCLEO-H755ZI-Q`
- MCU/board revision: `not recorded`
- Core under test: `CM7`
- HardRT branch: `develop`
- HardRT SHA: `c2585fa4cc299f0b2e3f306b25b62836b5dbb1a7`
- HardRT tracked source state: **clean**
- HardRT untracked workspace files: **present**
- STM32CubeH7 root: `/home/dev/STM32Cube/Repository/STM32CubeH7`
- STM32CubeH7 SHA/state: `f5c0b7a2b1f6eb26fde150f72edb2d7deb647066` / `DIRTY`
- ARM GCC: `arm-none-eabi-gcc (15:10.3-2021.07-4) 10.3.1 20210621 (release)`
- GDB: `GNU gdb (Ubuntu 12.1-0ubuntu1~22.04.2) 12.1`
- OpenOCD: `Open On-Chip Debugger 0.11.0`
- CMake: `cmake version 3.22.1`
- Host: `Linux dev 6.8.0-136-generic #136~22.04.1-Ubuntu SMP PREEMPT_DYNAMIC Fri Jul  3 16:29:11 UTC  x86_64 x86_64 x86_64 GNU/Linux`
- Timing samples per case: `10000`
- LED observation duration: `10s`

OpenOCD/GDB sessions are managed by this runner; no additional terminal windows are required.

## Pre-run generated build cleanup

The runner removed these known generated directories before qualification:

- `build-cortex_m`
- `install-cortex_m`
- `examples/hardrt_h755_blinky/build-cortex_m`
- `examples/hardrt_h755_blinky_cpp/build-cortex_m`
- `examples/hardrt_h755_demo/build-cortex_m`
- `examples/hardrt_h755_dwt_timing/build-cortex_m`
- `examples/hardrt_h755_preemption/build-cortex_m`

## Untracked workspace files

```text
.codex
.idea/.gitignore
.idea/editor.xml
.idea/hardrt.iml
.idea/misc.xml
.idea/modules.xml
.idea/vcs.xml
```

## Test results

| Test | Result | PASS criterion | Notes |
|---|:---:|---|---|
| Board probe | **PASS** | OpenOCD connects to STM32H755 before qualification. | |
| C blinky | **PASS** | Build/flash succeeds; `g_example_error` stays 0; both task counters increase; expected LED rates observed for at least 10 s. | |
| C++ blinky | **PASS** | Build/flash succeeds; `g_example_error` stays 0; both task counters increase; expected LED rates observed for at least 10 s. | |
| Scheduler counter demo | **PASS** | Build/flash succeeds; `g_example_error` stays 0; all four task/exit counters increase. | |
| DWT `event_to_task` timing | **PASS** | 10,000 samples, no error, min <= avg <= max, timer configuration recorded. | min=1174 cycles, avg=1223 cycles, max=1548 cycles. |
| DWT `sem_isr_ready` timing | **PASS** | 10,000 samples, no error, min <= avg <= max, timer configuration recorded. | min=278 cycles, avg=278 cycles, max=280 cycles. |
| Fixed-priority hardware preemption | **PASS** | ISR wake dispatches higher-priority task before interrupted lower-priority Thread mode continues. | sequence `[1,2,3,4,0]`, `need_switch=1`. |
| PRIORITY_RR retained-quantum preemption | **FAIL** | Expected trace low-A -> high -> low-A -> low-B with retained unused quantum. | Firmware error `32`; observed sequence `[1,2,3,0,5]`: low-B runs before interrupted low-A resumes. |

## Qualification verdict

- Passed: **7**
- Failed: **1**
- Overall: **FAIL**

This is valid development failure evidence for #31/#58. It is not final release qualification evidence because the scheduler contract fails and the external STM32CubeH7 checkout was dirty.

## Archived source bundle identity

- Original uploaded bundle: `20260903T133251Z_c2585fa4.tar`
- SHA-256: `8d927a1d67261071d0e90272aefa76afff94c8aca610b01147ed5d77970609ab`

## Decisive raw evidence

### `timing_event_to_task_gdb.log`

```text
--- Timing target reached ---
SystemCoreClock=64000000 Hz
event_hz=1000 target_samples=10000
case=event_to_task
metric=TIM2 ISR software timestamp -> latency task continuation
NOTE: this is a composite software ISR-to-task response measurement; it is NOT hardware event-to-ISR latency.
HardRT timing profile=none (no kernel timing hooks in this image)

[RESULT]
count=10000
min=1174 cycles, avg=1223 cycles (sum/count=1223), max=1548 cycles
min=18343 ns, avg=19109 ns, max=24187 ns
error=0

[TIMER CFG]
TIM2: PSC=31 ARR=999
RESULT: PASS
```

### `timing_sem_isr_ready_gdb.log`

```text
--- Timing target reached ---
SystemCoreClock=64000000 Hz
event_hz=1000 target_samples=10000
case=sem_isr_ready
metric=hrt_sem_give_from_isr entry -> waiter marked READY
HardRT timing profile=ipc (only IPC hook points enabled)

[RESULT]
count=10000
min=278 cycles, avg=278 cycles (sum/count=278), max=280 cycles
min=4343 ns, avg=4343 ns, max=4375 ns
error=0

[TIMER CFG]
TIM2: PSC=31 ARR=999
RESULT: PASS
```

### `preemption_priority_gdb.log`

```text
--- HardRT preemption validation stopped ---
case=1 pass=1 error=0
irq_count=1 need_switch=1 high_runs=1
low_a_counter=1 low_b_counter=0
ticks: A-start=0 IRQ=0 high=0 A-resume=0 B-first=0
RR remaining: expected=0 observed=0
sequence slots: [1, 2, 3, 4, 0]
RESULT: PASS
```

### `preemption_priority_rr_gdb.log`

```text
--- HardRT preemption validation stopped ---
case=2 pass=0 error=32
irq_count=1 need_switch=1 high_runs=1
low_a_counter=0 low_b_counter=1
ticks: A-start=0 IRQ=0 high=0 A-resume=0 B-first=0
RR remaining: expected=0 observed=0
sequence slots: [1, 2, 3, 0, 5]
RESULT: FAIL (error=32)
```

## Completion

- UTC end: `2026-09-03T13:35:38+00:00`
- Local end: `2026-09-03T15:35:38+02:00`
- Runner: `scripts/stm32_manual_test_full.sh`
