#!/usr/bin/env bash

# build HardRT and hardrt_h755_dwt_timing application
$(pwd)/scripts/build-lib-stm32h7xx.sh --app $(pwd)/examples/hardrt_h755_dwt_timing

# flash stm32h755
openocd -s /usr/share/openocd/scripts   -f scripts/openocd_h755_clean.cfg   -c "init; reset halt; \
      stm32h7x mass_erase 0; \
      stm32h7x mass_erase 1; \
      program examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf verify; \
      reset halt; shutdown"

# set up connection for dgb
# openocd -s /usr/share/openocd/scripts -f  scripts/openocd_h755.cfg -c "init; reset halt"

# Terminal 2 start timing statistics dbg script.
# gdb-multiarch -q examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf -batch -x scripts/gdb/timing.dbg