# HardRT STM32H755 Qualification Report

- Run ID: `20260903T143930Z_28f350d0`
- UTC start: `2026-09-03T14:39:36+00:00`
- Local start: `2026-09-03T16:39:36+02:00`
- Tester: `dev`
- Board: `NUCLEO-H755ZI-Q`
- MCU/board revision: `not recorded`
- Core under test: `CM7`
- HardRT branch: `develop`
- HardRT SHA: `28f350d0bce083f54b0afd4011b58330c33397a6`
- HardRT tracked source state: **clean**
- Tracked qualification evidence state: **DIRTY**
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

## Tracked qualification evidence changes

```text
D  validation/stm32/20260903T133251Z_c2585fa4/qualification.md
```

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

| Test | Result | PASS criterion | Evidence | Notes |
|---|:---:|---|---|---|
| Board probe | **PASS** | OpenOCD connects to STM32H755 before qualification. | `raw/00_probe.log` |   |
| C blinky | **PASS** | Build/flash succeeds; g_example_error stays 0; both task counters increase; LD1/PB0 toggles every 250 ms and LD2/PE1 every 500 ms is visibly observed for at least 10s. | `raw/c_blinky_*.log` |   |
| C++ blinky | **PASS** | Build/flash succeeds; g_example_error stays 0; both task counters increase; LD1/PB0 toggles every 100 ms and LD2/PE1 every 250 ms is visibly observed for at least 10s. | `raw/cpp_blinky_*.log` |   |
| Scheduler counter demo | **PASS** | Build/flash succeeds; g_example_error stays 0; dbg_counterA/B and dbg_exit_counterA/B all increase. | `raw/counter_demo_*.log` |   |
| DWT event_to_task timing | **PASS** | Build/flash succeeds; timing_target_reached is observed; no kernel/example error; count equals 10000; min <= avg <= max; SystemCoreClock and TIM2 PSC/ARR are recorded. | `raw/timing_event_to_task_*.log` | count=10000, min=1619 cycles, avg=1688 cycles, max=2654 cycles. |
| DWT sem_isr_ready timing | **PASS** | Build/flash succeeds; timing_target_reached is observed; no kernel/example error; count equals 10000; min <= avg <= max; SystemCoreClock and TIM2 PSC/ARR are recorded. | `raw/timing_sem_isr_ready_*.log` | count=10000, min=228 cycles, avg=250 cycles, max=271 cycles. |
| Fixed-priority hardware preemption | **PASS** | ISR wake dispatches the higher-priority task before interrupted low-priority Thread mode continues; validator reports RESULT: PASS. | `raw/preemption_priority_*.log` | case=1 pass=1 error=0 irq_count=1 need_switch=1 high_runs=1 ticks: A-start=0 IRQ=0 high=0 A-resume=0 B-first=0 RR remaining: expected=0 observed=0 sequence slots: [1, 2, 3, 4, 0] RESULT: PASS |
| PRIORITY_RR retained-quantum preemption | **PASS** | Trace is low-A -> high -> low-A -> low-B and low-A retains unused RR quantum; validator reports RESULT: PASS. | `raw/preemption_priority_rr_*.log` | case=2 pass=1 error=0 irq_count=1 need_switch=1 high_runs=1 ticks: A-start=0 IRQ=2 high=2 A-resume=2 B-first=20 RR remaining: expected=18 observed=18 sequence slots: [1, 2, 3, 4, 5] RESULT: PASS |

## Qualification verdict

- Passed: **8**
- Failed: **0**
- Overall: **PASS**

## Tester notes

None.

## Completion

- UTC end: `2026-09-03T14:42:09+00:00`
- Local end: `2026-09-03T16:42:09+02:00`
- Runner: `scripts/stm32_manual_test_full.sh`
