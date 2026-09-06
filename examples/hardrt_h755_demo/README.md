# HardRT H755 Demo

This NUCLEO-H755ZI-Q / CM7 example provides a minimal two-task HardRT application on STM32H755.

- Task A runs every 5 seconds.
- Task B runs every 10 seconds.

The example is used by the physical validation suite to verify scheduler/task progress on the Cortex-M port.

Build it with:

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-demo.sh
```

For toolchain, OpenOCD, and debugging setup, see [CROSSCOMPILE.md](../../docs/CROSSCOMPILE.md). For release qualification, use the consolidated [`stm32_manual_test_full.sh`](../../scripts/stm32_manual_test_full.sh) runner.
