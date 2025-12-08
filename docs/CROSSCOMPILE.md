# Cross Compile

To compile the library for a different architecture use the following instructions:

## ARM BAREMETAL

### Pre-requisites

#### prepare your projects:
navigate to `Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates/gcc/` from the `STM32CubeH7` repo and move your device
specific system, startup and linker files to their dedicated folder.

install gcc by version 

Debian/Ubuntu names shown; use your distro equivalents

```bash

sudo apt-get install gcc-arm-none-eabi gdb-multiarch openocd stlink-tools

```



### Build

use the following command to build the project
```bash

cmake -S . -B build-mcu \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
  -DHEARTOS_PORT=cortex_m \
  -DHEARTOS_BUILD_EXAMPLES=OFF
  

cmake --build build-mcu -j
cmake --install build-mcu --prefix "$PWD/install"
```

> **NOTE:** This example builds for `cortex-m7`, if you are building for a different processor update it accordingly. 



### Flashing the stm32

give st-link access to usb
```bash
cat <<'RULE' | sudo tee /etc/udev/rules.d/99-stlink.rules
# STMicroelectronics ST-LINK/V2 & V3
ATTRS{idVendor}=="0483", MODE:="0666"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger
```
probe the existence of the board
```bash
lsusb | grep -i st

```
ask package manager where the configuration scripts are located.

```bash
dpkg -L openocd | grep '/scripts$'

```
check for st file existence.
```bash
ls $(dpkg -L openocd | grep '/scripts$')/interface/stlink.cfg
ls $(dpkg -L openocd | grep '/scripts$')/target/stm32h7x_dual_bank.cfg
```
if they are shown the board can be flashed.
run the following openocd command to flash board:
```bash

openocd -s /usr/share/openocd/scripts   -f scripts/openocd_h755_clean.cfg   -c "init; reset halt; \
      stm32h7x mass_erase 0; \
      stm32h7x mass_erase 1; \
      program examples/heartos_h755_demo/build-cortex_m/heartos_cm7_demo.elf verify; \
      reset halt; shutdown"

```
if no errors pop up we can try and debug using dbg and 2 terminals:

Terminal A:
```bash

openocd -s /usr/share/openocd/scripts -f  scripts/openocd_h755.cfg -c "init; reset halt"
```
Which will launch the board in debug mode. And now we can uce gdb to debug for ticks.

Terminal B:
```bash
gdb-multiarch examples/heartos_h755_demo/build-cortex_m/heartos_cm7_demo.elf

```
inside (gdb)
```bash 
target extended-remote :3333
monitor reset halt
b SysTick_Handler            # PendSV_Handler
c

```

commands
```bash
gdb-multiarch -q examples/heartos_h755_demo/build-cortex_m/heartos_cm7_demo.elf -batch -x scripts/gdb/sp_check.gdb


```



### include in your project as follows:
#### CMake

#### Others?