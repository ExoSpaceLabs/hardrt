# Cross-Compilation

To compile the library for a different architecture, follow these instructions:

## ARM BARE-METAL

### Prerequisites

#### Project Preparation:
Navigate to `Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates/gcc/` from the `STM32CubeH7` repository and move the 
device-specific system, startup, and linker files to the dedicated folder.

Install GCC:
Debian/Ubuntu names shown; use equivalent names for other distributions.

```bash

sudo apt-get install gcc-arm-none-eabi gdb-multiarch openocd stlink-tools

```

# Use the following command to build the project:
```bash

cmake -S . -B build-mcu \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
  -DHARDRT_PORT=cortex_m \
  -DHARDRT_BUILD_EXAMPLES=OFF
  

cmake --build build-mcu -j
cmake --install build-mcu --prefix "$PWD/install"
```

> **NOTE:** This example builds for `cortex-m7`. If building for a different processor, update it accordingly. 

### Flashing the STM32

Provide st-link access to USB:
```bash
cat <<'RULE' | sudo tee /etc/udev/rules.d/99-stlink.rules
# STMicroelectronics ST-LINK/V2 & V3
ATTRS{idVendor}=="0483", MODE:="0666"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger
```
Probe the existence of the board:
```bash
lsusb | grep -i st

```
Query the package manager for the location of the configuration scripts.

```bash
dpkg -L openocd | grep '/scripts$'

```
# Check for ST file existence.
ls $(dpkg -L openocd | grep '/scripts$')/interface/stlink.cfg
ls $(dpkg -L openocd | grep '/scripts$')/target/stm32h7x_dual_bank.cfg
```
If these files are present, the board can be flashed.
Run the following openocd command to flash the board:
```bash

openocd -s /usr/share/openocd/scripts -f scripts/openocd_h755_clean.cfg -c "init; reset halt; \
      stm32h7x mass_erase 0; \
      stm32h7x mass_erase 1; \
      program examples/hardrt_h755_demo/build-cortex_m/hardrt_cm7_demo.elf verify; \
      reset halt; shutdown"

```
If no errors occur, debugging is possible using gdb and two terminals:

Terminal A:
```bash

openocd -s /usr/share/openocd/scripts -f scripts/openocd_h755.cfg -c "init; reset halt"
```
This command launches the board in debug mode. gdb can then be used to debug for ticks.

Terminal B:
```bash
gdb-multiarch examples/hardrt_h755_demo/build-cortex_m/hardrt_cm7_demo.elf

```
Inside (gdb):
```bash 
target extended-remote :3333
monitor reset halt
b SysTick_Handler # PendSV_Handler
c

```
Alternatively, run a gdb script that prepares all required breakpoints and outputs. See example scripts under
scripts/gdb/.

Command:
```bash

gdb-multiarch -q examples/hardrt_h755_demo/build-cortex_m/hardrt_cm7_demo.elf -batch -x scripts/gdb/tasks.gdb
```
This example script sets breakpoints for taskA and taskB with additional information like ticks and execution count.


### Include HardRT in a project:
#### CMake

#### Others