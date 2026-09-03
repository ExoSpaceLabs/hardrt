cmake_minimum_required(VERSION 3.16)
project(hardrt_h755_preemption LANGUAGES C ASM)

set(STM32CUBE_H7_ROOT "" CACHE STRING "STM32CubeH7 repository path")
set(STM32_DEVICE "STM32H755xx" CACHE STRING "STM32 device define")
set(STM32_CORE "CORE_CM7" CACHE STRING "Core define")
set(HARDRT_PREEMPT_CASE "priority" CACHE STRING "Validation case: priority|priority_rr")
set_property(CACHE HARDRT_PREEMPT_CASE PROPERTY STRINGS priority priority_rr)

if(STM32CUBE_H7_ROOT STREQUAL "")
    set(STM32CUBE_H7_ROOT "/home/dev/STM32Cube/Repository/STM32CubeH7")
endif()
if(NOT EXISTS "${STM32CUBE_H7_ROOT}")
    message(FATAL_ERROR "Provide a valid STM32CubeH7 path. ${STM32CUBE_H7_ROOT} does not exist.")
endif()
if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Set -DCMAKE_TOOLCHAIN_FILE=.../arm-none-eabi.cmake")
endif()

if(HARDRT_PREEMPT_CASE STREQUAL "priority")
    set(HARDRT_PREEMPT_CASE_ID 1)
elseif(HARDRT_PREEMPT_CASE STREQUAL "priority_rr")
    set(HARDRT_PREEMPT_CASE_ID 2)
else()
    message(FATAL_ERROR "Unknown HARDRT_PREEMPT_CASE=${HARDRT_PREEMPT_CASE}")
endif()

find_package(HardRT REQUIRED)

set(COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../hardrt_h755_demo")
set(MCU_OPTS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

add_executable(${PROJECT_NAME}.elf
    ${COMMON_DIR}/startup/startup_stm32h755xx.s
    ${COMMON_DIR}/vendor/stm32/system_stm32h7xx.c
    ${COMMON_DIR}/src/syscalls.c
    ${COMMON_DIR}/src/system.c
    src/main.c
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Core/Include
    ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${COMMON_DIR}/inc
)
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    ${STM32_DEVICE}
    ${STM32_CORE}
    HRT_PREEMPT_CASE_ID=${HARDRT_PREEMPT_CASE_ID}
)
target_compile_options(${PROJECT_NAME}.elf PRIVATE
    ${MCU_OPTS} -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE HardRT::hardrt)
target_link_options(${PROJECT_NAME}.elf PRIVATE
    ${MCU_OPTS}
    -T${COMMON_DIR}/linker/stm32h755xx_flash_CM7.ld
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map
    -specs=nano.specs
    -specs=nosys.specs
    -nostartfiles
)
