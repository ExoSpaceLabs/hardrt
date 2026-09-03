cmake_minimum_required(VERSION 3.16)
project(hardrt_cm7_dwt_timing LANGUAGES C ASM)

set(STM32CUBE_H7_ROOT "" CACHE STRING "STM32CubeH7 repository path")
set(STM32_DEVICE "STM32H755xx" CACHE STRING "STM32 device define")
set(STM32_CORE "CORE_CM7" CACHE STRING "Core define")
set(HARDRT_TIMING_CASE "event_to_task" CACHE STRING "Timing case: event_to_task|sem_isr_ready|ready_to_task|scheduler_decision")
set_property(CACHE HARDRT_TIMING_CASE PROPERTY STRINGS event_to_task sem_isr_ready ready_to_task scheduler_decision)
set(HARDRT_TIMING_EVENT_HZ 1000 CACHE STRING "One-shot event rate used between samples")
set(HARDRT_TIMING_TARGET_SAMPLES 10000 CACHE STRING "Samples collected before debugger breakpoint")

if(STM32CUBE_H7_ROOT STREQUAL "")
    set(STM32CUBE_H7_ROOT "/home/dev/STM32Cube/Repository/STM32CubeH7")
    message("-- setting STM32CUBE_H7_ROOT to default value ${STM32CUBE_H7_ROOT}")
endif()
if(NOT EXISTS "${STM32CUBE_H7_ROOT}")
    message(FATAL_ERROR "Provide a valid STM32CubeH7 path. ${STM32CUBE_H7_ROOT} does not exist.")
endif()

if(HARDRT_TIMING_CASE STREQUAL "event_to_task")
    set(HARDRT_TIMING_CASE_ID 1)
elseif(HARDRT_TIMING_CASE STREQUAL "sem_isr_ready")
    set(HARDRT_TIMING_CASE_ID 2)
elseif(HARDRT_TIMING_CASE STREQUAL "ready_to_task")
    set(HARDRT_TIMING_CASE_ID 3)
elseif(HARDRT_TIMING_CASE STREQUAL "scheduler_decision")
    set(HARDRT_TIMING_CASE_ID 4)
else()
    message(FATAL_ERROR "Unknown HARDRT_TIMING_CASE=${HARDRT_TIMING_CASE}")
endif()

message("-- HARDRT_TIMING_CASE           : ${HARDRT_TIMING_CASE}")
message("-- HARDRT_TIMING_EVENT_HZ       : ${HARDRT_TIMING_EVENT_HZ}")
message("-- HARDRT_TIMING_TARGET_SAMPLES : ${HARDRT_TIMING_TARGET_SAMPLES}")

set(STM32CUBE_INCLUDE_DIRS
    ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Core/Include
    ${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${STM32CUBE_H7_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc
    inc
)

if(NOT CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Set -DCMAKE_TOOLCHAIN_FILE=.../arm-none-eabi.cmake")
endif()

find_package(HardRT REQUIRED)

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
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    ${STM32_DEVICE}
    ${STM32_CORE}
    HRT_TIMING_CASE_ID=${HARDRT_TIMING_CASE_ID}
    HRT_TIMING_EVENT_HZ=${HARDRT_TIMING_EVENT_HZ}
    HRT_TIMING_TARGET_SAMPLES=${HARDRT_TIMING_TARGET_SAMPLES}
)
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

if(HARDRT_TIMING_CASE STREQUAL "scheduler_decision")
    # Diagnostic-only link wrapping. PendSV's call to hrt__schedule is redirected
    # through the timing wrapper in main.c; normal HardRT builds are untouched.
    target_link_options(${PROJECT_NAME}.elf PRIVATE -Wl,--wrap=hrt__schedule)
endif()
