# HardRT H755 Blinky

This example application shows how HardRT can be used on the stm32h755 project.

identical to hardrt_h755_demo, with the inclusion of LED blinking for each task.

The RTOS runs two tasks:
- TaskA at every 5s toggling red LED
- TaskB at every 10s toggling green LED

To build refer to [CROSSCOMPILE.md](../../docs/CROSSCOMPILE.md) document. Which will also 
describe how to run example scripts.

> NOTE: if HARDRT_BDG_VARIABLES are disabled only tasks.gdb can be executed.