# HardRT demo H755

This example application shows how HardRT can be used on the stm32h755 project.

The RTOS runs two tasks:
- TaskA at every 5s
- TaskB at every 10s

To build refer to [CROSSCOMPILE.md](../../docs/CROSSCOMPILE.md) document. Which will also 
describe how to run example scripts.

> NOTE: if HARDRT_BDG_VARIABLES are disabled only tasks.gdb can be executed.