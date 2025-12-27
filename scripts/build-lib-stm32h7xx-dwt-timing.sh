#!/usr/bin/env bash

$(pwd)/scripts/build-lib-stm32h7xx.sh --app $(pwd)/examples/hardrt_h755_dwt_timing}
# CLI overrides (optional)

openocd -s /usr/share/openocd/scripts   -f scripts/openocd_h755_clean.cfg   -c "init; reset halt; \
      stm32h7x mass_erase 0; \
      stm32h7x mass_erase 1; \
      program examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf verify; \
      reset halt; shutdown"
