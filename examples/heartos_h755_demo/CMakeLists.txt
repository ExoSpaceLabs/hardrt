cmake_minimum_required(VERSION 3.16)
project(heartos_cm7_demo LANGUAGES C ASM)

# your arm-none-eabi toolchain file
if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Set -DCMAKE_TOOLCHAIN_FILE=.../arm-none-eabi.cmake")
endif()

find_package(HeaRTOS REQUIRED)  # from your installed heartos

set(MCU "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

add_executable(${PROJECT_NAME}.elf
        startup/startup_stm32h755xx.s
        vendor/stm32/system_stm32h7xx.c
        src/main.c
)

target_compile_options(${PROJECT_NAME}.elf PRIVATE
        ${MCU} -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE HeaRTOS::heartos)
target_link_options(${PROJECT_NAME}.elf PRIVATE
        ${MCU}
        -T${CMAKE_SOURCE_DIR}/linker/stm32h755xx_flash_CM7.ld
        -Wl,--gc-sections -Wl,-Map=${PROJECT_NAME}.map
        -nostartfiles -nostdlib -lc -lm -lnosys
)
