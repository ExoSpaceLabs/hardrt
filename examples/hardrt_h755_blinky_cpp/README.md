# HardRT H755 C++ Blinky

This NUCLEO-H755ZI-Q / CM7 example exercises the optional C++17 HardRT wrapper while two tasks drive board LEDs through the STM32 HAL.

- Task A toggles the red LED every 5 seconds.
- Task B toggles the green LED every 10 seconds.

It is part of the physical qualification matrix and verifies the C++ wrapper path on the real Cortex-M target.

Build the standalone example with:

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-blinky-cpp.sh
```

For toolchain, OpenOCD, and debugging setup, see [CROSSCOMPILE.md](../../docs/CROSSCOMPILE.md). For release qualification, use the consolidated [`stm32_manual_test_full.sh`](../../scripts/stm32_manual_test_full.sh) runner rather than manually assembling evidence from individual examples.
