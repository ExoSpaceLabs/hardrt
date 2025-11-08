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

### include in your project as follows:
#### CMake

#### Others?