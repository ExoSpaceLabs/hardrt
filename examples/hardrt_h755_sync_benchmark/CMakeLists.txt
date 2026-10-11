cmake_minimum_required(VERSION 3.16)
project(hardrt_h755_sync_benchmark LANGUAGES C ASM)
set(STM32CUBE_H7_ROOT "" CACHE PATH "STM32CubeH7 checkout")
set(HARDRT_BENCH_SAMPLES 1000 CACHE STRING "DWT samples per operation")
if(NOT HARDRT_BENCH_SAMPLES MATCHES "^[1-9][0-9]*$" OR HARDRT_BENCH_SAMPLES GREATER 100000)
  message(FATAL_ERROR "HARDRT_BENCH_SAMPLES must be 1..100000")
endif()
if(NOT EXISTS "${STM32CUBE_H7_ROOT}/Drivers/CMSIS/Core/Include")
  message(FATAL_ERROR "Set STM32CUBE_H7_ROOT to a STM32CubeH7 checkout")
endif()
if(NOT CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Set the arm-none-eabi toolchain file")
endif()
find_package(HardRT REQUIRED)
set(COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../hardrt_h755_demo")
set(MCU_OPTS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
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
 STM32H755xx CORE_CM7 HRT_SYNC_BENCH_SAMPLES=${HARDRT_BENCH_SAMPLES}
)
target_compile_options(${PROJECT_NAME}.elf PRIVATE
 ${MCU_OPTS} -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections
)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE HardRT::hardrt)
target_link_options(${PROJECT_NAME}.elf PRIVATE
 ${MCU_OPTS} -T${COMMON_DIR}/linker/stm32h755xx_flash_CM7.ld
 -Wl,--gc-sections -Wl,-Map=${PROJECT_NAME}.map
 -specs=nano.specs -specs=nosys.specs -nostartfiles
)
