# HardRT timing on H755

This example application is intended to extract timing statistics from the device.


to build HardRT, app and flash stm32h755 execute:
```bash
./build-lib-stm32h7xx-dwt-timing.sh
```
set up dbg remote connection:
```bash
openocd -s /usr/share/openocd/scripts -f  scripts/openocd_h755.cfg -c "init; reset halt"

```

open another terminal and run:
```bash

gdb-multiarch -q examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf -batch -x scripts/gdb/timing.dbg
```

To build refer to [CROSSCOMPILE.md](../../docs/CROSSCOMPILE.md) document. Which will also
describe how to run example scripts.
