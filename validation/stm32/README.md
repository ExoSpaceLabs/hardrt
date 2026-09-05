# STM32H755 hardware qualification evidence

Use one manual entry point for physical-board validation:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The script runs the complete NUCLEO-H755ZI-Q qualification matrix: **13 functional contracts** plus **22 hardware benchmark images**. It prints a case-by-case summary and writes development evidence by default under:

```text
validation/stm32/<UTC>_<short-sha>/
```

Timestamped development-run directories are ignored by Git, so they remain available for local inspection without polluting repository status.

Development shortcuts are available through the same entry point:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --only functional
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --only benchmark
```

A filtered run is useful during development but is not complete release evidence.

For a release candidate, run the unfiltered script from the exact release SHA with a clean HardRT source tree and clean/pinned STM32CubeH7 checkout. After reviewing a full `13/13 PASS` functional result and `22/22 PASS` benchmark result, manually copy or move that selected run into:

```text
validation/stm32/releases/vX.Y.Z/
```

Keep exactly one committed hardware qualification package per release.

## Functional hardware matrix

The board/OpenOCD probe is a prerequisite and is reported separately.

1. C blinky/task integration
2. C++ blinky/task integration
3. scheduler counter demo
4. fixed-priority ISR preemption
5. global RR mixed-priority scheduling
6. `PRIORITY_RR` retained-quantum/queue-precedence validation
7. semaphore hardware contract
8. queue hardware contract
9. mutex hardware contract
10. event-flags hardware contract, including task/real-ISR producers and `need_switch`
11. task-notification hardware contract, including pending/unrelated-IPC behavior and real-ISR wake
12. external TIM2-driven tick contract
13. BASEPRI critical-section contract

## Hardware benchmark matrix

The benchmark suite contains four latency/switch images plus an 18-point tick/sleeper scaling matrix:

- DWT `event_to_task`
- DWT `sem_isr_ready`
- DWT `ready_to_task`
- DWT `scheduler_decision`
- tick/sleeper scenarios `none`, `one_sleep`, `all_sleep`, `one_expiry`, `simultaneous`, and `staggered` at configured application-task capacities 8, 16, and 32

LED observations are qualitative. The tester confirms both LEDs toggle and the configured relative rate difference is clearly visible; automated counters and DWT fixtures provide the quantitative execution/timing evidence.
