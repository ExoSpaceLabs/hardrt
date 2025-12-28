cmake_minimum_required(VERSION 3.16)
project(hardrt_h755_blinky LANGUAGES C ASM)


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
    message(FATAL_ERROR "Please provide a valid the path for the cloned STM32CubeH7. ${STM32CUBE_H7_ROOT} does not exist.")
else ()
    message("-- STM32CUBE_H7_ROOT ${STM32CUBE_H7_ROOT} found.")
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
    message(FATAL_ERROR "Set -DCMAKE_TOOLCHAIN_FILE=.../arm-none-eabi.cmake")
endif()

find_package(HardRT REQUIRED)  # from your installed hardrt

set(MCU_OPTS
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
)

add_executable(${PROJECT_NAME}.elf
        startup/startup_stm32h755xx.s
        vendor/stm32/system_stm32h7xx.c
        src/syscalls.c
        src/system.c
        src/main.c
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE ${STM32CUBE_INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${STM32_DEVICE} ${STM32_CORE})

target_compile_options(${PROJECT_NAME}.elf PRIVATE
        ${MCU_OPTS} -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE HardRT::hardrt)
target_link_options(${PROJECT_NAME}.elf PRIVATE
        ${MCU_OPTS}
        -T${CMAKE_SOURCE_DIR}/linker/stm32h755xx_flash_CM7.ld
        -Wl,--gc-sections
        -Wl,-Map=${PROJECT_NAME}.map
        -specs=nano.specs
        -specs=nosys.specs
        -nostartfiles
)
