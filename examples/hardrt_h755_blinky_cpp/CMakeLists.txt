cmake_minimum_required(VERSION 3.16)
project(hardrt_h755_blinky_cpp LANGUAGES C CXX ASM)

# -----------------------------
# Config knobs (cache variables)
# -----------------------------

set(STM32CUBE_H7_ROOT "" CACHE STRING "STM32CubeH7 repository path: clone git repo")
set(STM32_DEVICE "STM32H755xx" CACHE STRING "STM32 device define (e.g. STM32H755xx)")
set(STM32_CORE   "CORE_CM7"     CACHE STRING "Core define: CORE_CM7 or CORE_CM4")

if(STM32CUBE_H7_ROOT STREQUAL "")
    set(STM32CUBE_H7_ROOT "/home/dev/STM32Cube/Repository/STM32CubeH7")
    message("-- setting STM32CUBE_H7_ROOT to default value ${STM32CUBE_H7_ROOT}")
endif ()

if(NOT EXISTS ${STM32CUBE_H7_ROOT})
    message(WARNING "Please provide a valid the path for the cloned STM32CubeH7. ${STM32CUBE_H7_ROOT} does not exist.")
endif ()

set(STM32CUBE_INCLUDE_DIRS
        ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Core/Include
        ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
        ${STM32CUBE_H7_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc
        ${STM32CUBE_H7_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
        ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
        ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Include
        inc
)

# your arm-none-eabi toolchain file
if(NOT CMAKE_TOOLCHAIN_FILE)
    # If not provided, we might be in-tree.
endif()

set(MCU_OPTS
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
)

set(ELF_NAME "${PROJECT_NAME}.elf")

add_executable(${ELF_NAME}
        startup/startup_stm32h755xx.s
        vendor/stm32/system_stm32h7xx.c
        src/syscalls.c
        src/system.c
        src/main.cpp
)

target_include_directories(${ELF_NAME} PRIVATE ${STM32CUBE_INCLUDE_DIRS})
target_compile_definitions(${ELF_NAME} PRIVATE ${STM32_DEVICE} ${STM32_CORE})

target_compile_options(${ELF_NAME} PRIVATE
        ${MCU_OPTS} -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
)


if (TARGET hardrt)
    # In-tree build
    target_link_libraries(${ELF_NAME} PRIVATE hardrtpp)
    target_include_directories(${ELF_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/cpp)
else()
    # Out-of-tree (after install) usage
    find_package(HardRT REQUIRED)
    target_link_libraries(${ELF_NAME} PRIVATE HardRT::hardrtpp)
endif()



target_link_options(${ELF_NAME} PRIVATE
        ${MCU_OPTS}
        -T${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32h755xx_flash_CM7.ld
        -Wl,--gc-sections
        -Wl,-Map=${PROJECT_NAME}.map
        -specs=nano.specs
        -specs=nosys.specs
        -nostartfiles
)
