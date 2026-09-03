# STM32H755 hardware qualification evidence

Use one manual entry point for physical-board validation:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The script runs the complete NUCLEO-H755ZI-Q hardware suite, prints a case-by-case summary plus DWT timing statistics, and writes development evidence under the gitignored local directory:

```text
.qualification/stm32/<UTC>_<short-sha>/
```

Development runs are not committed.

The suite contains 13 functional/hardware-contract cases plus one scheduler timing diagnostic. The diagnostic is included because scheduler performance is part of the hard-real-time evidence, but it does not change the meaning of the already established 13/13 functional hardware result.

For a release candidate, run the same script from the exact release SHA with a clean HardRT source tree and clean/pinned STM32CubeH7 checkout. After reviewing a full passing report, manually copy or move that single selected run into:

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
8. DWT `scheduler_decision` diagnostic
9. fixed-priority ISR preemption
10. `PRIORITY_RR` retained-quantum/queue-precedence validation
11. semaphore hardware contract
12. queue hardware contract
13. mutex hardware contract
14. external TIM2-driven tick contract

For focused performance investigation, the same script can run only the scheduler diagnostic:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --only scheduler_decision
```

That targeted mode is development evidence only and must not be used as release qualification evidence.

LED observations are qualitative. The tester confirms both LEDs toggle and the configured relative rate difference is clearly visible; exact millisecond timing is verified through automated counters/timing fixtures rather than eyesight.
