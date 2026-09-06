# Cross-compilation and STM32H755 development

HardRT ships a GNU Arm Embedded CMake toolchain and STM32H755 validation examples. The library itself does not vendor STM32CubeH7; board examples consume CMSIS/HAL headers from a separate STM32CubeH7 checkout.

## Prerequisites

On Debian/Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build make \
  gcc-arm-none-eabi gdb-multiarch \
  openocd stlink-tools
```

For the supplied NUCLEO-H755ZI-Q examples, obtain STM32CubeH7 separately and keep the checkout path available, for example:

```text
/home/dev/STM32Cube/Repository/STM32CubeH7
```

The validation scripts expect at least:

```text
Drivers/CMSIS/Core/Include
Drivers/CMSIS/Device/ST/STM32H7xx/Include
Drivers/STM32H7xx_HAL_Driver/Inc
```

Do not copy or move STM32Cube files into the HardRT source tree.

## Cross-build the HardRT library

From the repository root:

```bash
cmake -S . -B build-cortex_m -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DHARDRT_PORT=cortex_m \
  -DHARDRT_BUILD_TESTS=OFF \
  -DHARDRT_BUILD_EXAMPLES=OFF \
  -DHARDRT_ENABLE_CPP=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake

cmake --build build-cortex_m --parallel
cmake --install build-cortex_m --prefix "$PWD/install-cortex_m"
```

Enable the optional C++17 wrapper target with `-DHARDRT_ENABLE_CPP=ON` when `arm-none-eabi-g++` is available.

## Cross-build the STM32H755 validation examples

The CI-equivalent helper builds the Cortex-M library and the complete STM32H755 example matrix without flashing hardware:

```bash
export STM32CUBE_H7_ROOT=/path/to/STM32CubeH7
./scripts/build-stm32-examples-ci.sh
```

CI runs this on Ubuntu 22.04 with `gcc-arm-none-eabi`. It covers the C/C++ board examples, scheduler/IPC fixtures, signal timing builders, and tick/sleeper benchmark configurations.

## Build an individual H755 example

The repository provides helpers for the physical examples. For example:

```bash
STM32CUBE_H7_ROOT=/path/to/STM32CubeH7 \
  ./scripts/build-lib-stm32h7xx-demo.sh
```

The common helper installs HardRT first and then builds the application against the installed CMake package. Individual example scripts select the required application and configuration.

## ST-Link permissions

If your distribution does not already provide suitable udev rules, a simple development rule is:

```bash
cat <<'RULE' | sudo tee /etc/udev/rules.d/99-stlink.rules
ATTRS{idVendor}=="0483", MODE:="0666"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Confirm the probe is visible:

```bash
lsusb | grep -i st
```

## Flash and debug

The repository contains OpenOCD configurations for the NUCLEO-H755ZI-Q. A clean flash of the demo image can be performed with:

```bash
openocd -s /usr/share/openocd/scripts \
  -f scripts/openocd_h755_clean.cfg \
  -c "init; reset halt; \
      stm32h7x mass_erase 0; \
      stm32h7x mass_erase 1; \
      program examples/hardrt_h755_demo/build-cortex_m/hardrt_cm7_demo.elf verify; \
      reset halt; shutdown"
```

Start a debug server with:

```bash
openocd -s /usr/share/openocd/scripts \
  -f scripts/openocd_h755.cfg \
  -c "init; reset halt"
```

Then connect GDB in another terminal:

```bash
gdb-multiarch examples/hardrt_h755_demo/build-cortex_m/hardrt_cm7_demo.elf
```

Inside GDB:

```gdb
target extended-remote :3333
monitor reset halt
continue
```

Repository GDB scripts under `scripts/gdb/` provide deterministic validation/measurement procedures for the supplied fixtures.

## Complete physical qualification

Do not assemble release evidence by manually running individual examples. The supported human-facing qualification entry point is:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

It owns board probing, build/flash cycles, functional validation, timing collection, and evidence packaging. See [STM32_MANUAL_TESTS.md](STM32_MANUAL_TESTS.md) and [QUALIFICATION.md](QUALIFICATION.md).

## Using HardRT from another CMake project

After installing a target-specific HardRT build:

```cmake
find_package(HardRT 0.5.0 REQUIRED)
target_link_libraries(my_firmware PRIVATE HardRT::hardrt)
```

When wrappers were enabled in the installed package:

```cmake
target_link_libraries(my_firmware PRIVATE HardRT::hardrtpp)
```

The application remains responsible for MCU startup code, linker script, clock/peripheral initialization, and any vendor HAL/CMSIS integration required by its board.
