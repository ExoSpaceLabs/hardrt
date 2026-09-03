# HardRT STM32H755 Qualification Report

- Run ID: `20260903T145719Z_28f350d0`
- UTC start: `2026-09-03T14:57:21+00:00`
- Local start: `2026-09-03T16:57:21+02:00`
- Tester: `dev`
- Board: `NUCLEO-H755ZI-Q`
- Core under test: `CM7`
- HardRT branch: `develop`
- HardRT SHA: `28f350d0bce083f54b0afd4011b58330c33397a6`
- HardRT tracked source state: **clean**
- Tracked qualification evidence state: **DIRTY**
- STM32CubeH7 SHA/state: `f5c0b7a2b1f6eb26fde150f72edb2d7deb647066` / **clean**
- ARM GCC: `10.3.1`
- GDB: `12.1`
- OpenOCD: `0.11.0`
- CMake: `3.22.1`
- Timing samples per case: `10000`
- Original uploaded tar SHA-256: `11a58cc8c06405bda909a2b4663cf99f0b439118dd9c8456b7ba2d4a3913bdc9`

## Test results

| Test | Result | Evidence summary |
|---|:---:|---|
| Board probe | **PASS** | OpenOCD connected to STM32H755. |
| C blinky | **PASS** | Counters advanced; visible LED rates accepted. |
| C++ blinky | **PASS** | Counters advanced; visible LED rates accepted. |
| Scheduler counter demo | **PASS** | Task and exit counters advanced with `g_example_error == 0`. |
| DWT `event_to_task` | **PASS** | 10000 samples; min/avg/max `1619 / 1688 / 2654` cycles. |
| DWT `sem_isr_ready` | **PASS** | 10000 samples; min/avg/max `228 / 250 / 271` cycles. |
| Fixed-priority hardware preemption | **PASS** | `case=1 pass=1 error=0`, sequence `[1,2,3,4,0]`. |
| `PRIORITY_RR` retained-quantum preemption | **PASS** | `case=2 pass=1 error=0`, sequence `[1,2,3,4,5]`, expected remaining `18`, observed `18`. |

## Qualification verdict

- Passed: **8**
- Failed: **0**
- Overall: **PASS**

## Decisive hardware transcripts

### Fixed-priority preemption

```text
case=1 pass=1 error=0
irq_count=1 need_switch=1 high_runs=1
low_a_counter=1 low_b_counter=0
sequence slots: [1, 2, 3, 4, 0]
RESULT: PASS
```

### PRIORITY_RR retained quantum

```text
case=2 pass=1 error=0
irq_count=1 need_switch=1 high_runs=1
low_a_counter=22728 low_b_counter=1
ticks: A-start=0 IRQ=2 high=2 A-resume=2 B-first=20
RR remaining: expected=18 observed=18
sequence slots: [1, 2, 3, 4, 5]
RESULT: PASS
```

### `event_to_task`

```text
SystemCoreClock=64000000 Hz
count=10000
min=1619 cycles
avg=1688 cycles
max=2654 cycles
error=0
TIM2: PSC=31 ARR=999
RESULT: PASS
```

### `sem_isr_ready`

```text
SystemCoreClock=64000000 Hz
count=10000
min=228 cycles
avg=250 cycles
max=271 cycles
error=0
TIM2: PSC=31 ARR=999
RESULT: PASS
```

This is strong clean-dependency development qualification evidence. Issue #56 remains open because its own acceptance criteria still require the eventual exact release-candidate SHA and resolution of any remaining dedicated internal/external tick-source hardware cases required by the final v0.5 tick contract.
