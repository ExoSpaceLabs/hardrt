# Hardware qualification policy

HardRT separates **development measurements** from **release qualification evidence**.

- Development STM32 runs are written under `.qualification/stm32/` and are gitignored.
- The preferred entry point is `scripts/stm32_qualification.sh`, which runs the complete current hardware matrix.
- Only the exact release-candidate SHA may be promoted with `scripts/promote_stm32_qualification.sh`.
- The repository stores exactly one promoted package per released version under `validation/stm32/releases/vX.Y.Z/`.
- Promotion requires the complete matrix to report `Full matrix overall: PASS`, clean tracked HardRT source, a clean recorded STM32CubeH7 checkout, and a run SHA matching current HEAD.

## Human observation

LED examples require only a qualitative visual check: both LEDs visibly toggle and their configured relative rates are distinguishable (for example, one is roughly twice as fast). Exact millisecond timing is not a human acceptance criterion. Automated counters and DWT measurements cover timing/progress evidence.

## What belongs on hardware

Hardware qualification focuses on behavior that can differ because of the Cortex-M port or real interrupt execution:

- PendSV/context switching and task progress;
- SysTick/external-tick integration;
- interrupt-safe synchronization and BASEPRI critical sections;
- scheduler-aware preemption and retained RR quantum;
- semaphore, queue, and mutex blocking/wake/handoff paths;
- C/C++ integration;
- latency measurements.

Pure argument validation and deterministic data-structure edge cases remain primarily hosted tests unless they interact with a port-specific path. Hardware validation is additive, not a replacement for the much broader hosted suite.
