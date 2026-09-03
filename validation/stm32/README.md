# STM32H755 hardware qualification evidence

Use one manual entry point for physical-board validation:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The script runs the complete 13-case NUCLEO-H755ZI-Q functional qualification matrix, prints a case-by-case summary plus DWT timing statistics, and writes development evidence by default under the visible repository path:

```text
validation/stm32/<UTC>_<short-sha>/
```

Timestamped development-run directories are ignored by Git, so they remain easy to inspect locally without polluting repository status.

The scheduler-decision timing measurement is a separate targeted diagnostic and can be run through the same entry point:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --only scheduler_decision --clean-builds
```

It is not counted as a fourteenth functional qualification case.

Latest development scheduler diagnostic on NUCLEO-H755ZI-Q / CM7 at 64 MHz, HardRT `7d208b5e`, 10,000 samples:

- `scheduler_decision`: 268 / 322 / 337 cycles (min/avg/max)

The diagnostic wraps `hrt__schedule()` only in the benchmark image, so production HardRT kernel code is unchanged. The uploaded development result itself remains local evidence; this README records only the measured summary.

For a release candidate, run the same full 13-case script from the exact release SHA with a clean HardRT source tree and clean/pinned STM32CubeH7 checkout. After reviewing a full `13/13 PASS`, manually copy or move that single selected run into:

```text
validation/stm32/releases/vX.Y.Z/
```

Keep exactly one committed hardware qualification package per release. No separate qualification or promotion script is required.

## Hardware matrix

1. board/OpenOCD probe
2. C blinky/task integration
3. C++ blinky/task integration
4. scheduler counter demo
5. DWT `event_to_task`
6. DWT `sem_isr_ready`
7. DWT `ready_to_task`
8. fixed-priority ISR preemption
9. `PRIORITY_RR` retained-quantum/queue-precedence validation
10. semaphore hardware contract
11. queue hardware contract
12. mutex hardware contract
13. external TIM2-driven tick contract

LED observations are qualitative. The tester confirms both LEDs toggle and the configured relative rate difference is clearly visible; exact millisecond timing is verified through automated counters/timing fixtures rather than eyesight.
