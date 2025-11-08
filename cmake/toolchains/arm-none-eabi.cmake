# cmake/toolchains/arm-none-eabi.cmake
# Generic bare-metal ARM toolchain (Cortex-M). No vendor HAL assumptions.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
# Prevent try_run: only try_compile static libs
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# --- Tool discovery ---
find_program(ARM_NONE_EABI_GCC    arm-none-eabi-gcc)
find_program(ARM_NONE_EABI_GPP    arm-none-eabi-g++)
find_program(ARM_NONE_EABI_AR     arm-none-eabi-ar)
find_program(ARM_NONE_EABI_RANLIB arm-none-eabi-ranlib)
find_program(ARM_NONE_EABI_AS     arm-none-eabi-as)
find_program(ARM_NONE_EABI_SIZE   arm-none-eabi-size)
find_program(ARM_NONE_EABI_OBJCOPY arm-none-eabi-objcopy)
find_program(ARM_NONE_EABI_OBJDUMP arm-none-eabi-objdump)

if(NOT ARM_NONE_EABI_GCC OR NOT ARM_NONE_EABI_GPP)
  message(FATAL_ERROR "arm-none-eabi-gcc/g++ not found. Install the ARM GCC toolchain.")
endif()

set(CMAKE_C_COMPILER   ${ARM_NONE_EABI_GCC})
set(CMAKE_CXX_COMPILER ${ARM_NONE_EABI_GPP})
set(CMAKE_AR           ${ARM_NONE_EABI_AR})
set(CMAKE_RANLIB       ${ARM_NONE_EABI_RANLIB})

#enable_language(ASM)
set(CMAKE_ASM_COMPILER ${ARM_NONE_EABI_GCC})

# Avoid host paths leaking into finds
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)

# ---- MCU flags ----
# Default flags are reasonable for STM32H7. Override with -DMCU_FLAGS="..."
if(NOT DEFINED MCU_FLAGS)
  set(MCU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
endif()

# Initialize CMake flags (projects may append per-target)
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS} -ffreestanding -fno-builtin -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS} -ffreestanding -fno-builtin -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -Wl,--gc-sections")
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# ---- Build-type defaults ----
# Let projects choose -O level; we just provide sane defaults.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_FLAGS_DEBUG          "-O0 -g3" CACHE INTERNAL "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG        "-O0 -g3" CACHE INTERNAL "" FORCE)
set(CMAKE_C_FLAGS_RELEASE        "-O2"     CACHE INTERNAL "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE      "-O2"     CACHE INTERNAL "" FORCE)
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3" CACHE INTERNAL "" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g3" CACHE INTERNAL "" FORCE)

# ---- Specs selection (app-level) ----
# You likely want nano newlib for examples; kernel can be freestanding.
# Override or append these in the executable target, not here, for flexibility:
#   target_link_options(app PRIVATE -specs=nano.specs -lc -lm -lnosys)
# If you go full freestanding for a target:
#   target_compile_options(kernel PRIVATE -ffreestanding -fno-builtin)
#   target_link_options(kernel PRIVATE -nostdlib -nostartfiles)

# ---- Linker script (app-level) ----
# Provide a variable that apps can pass: -DLINKER_SCRIPT=/path/to/STM32H755ZITx_FLASH.ld
if(DEFINED LINKER_SCRIPT)
  # Projects should apply this to executables:
  # target_link_options(app PRIVATE -T${LINKER_SCRIPT})
endif()

# Export some tools for post-build steps if needed
set(ARM_SIZE    ${ARM_NONE_EABI_SIZE}    CACHE FILEPATH "arm-none-eabi-size")
set(ARM_OBJCOPY ${ARM_NONE_EABI_OBJCOPY} CACHE FILEPATH "arm-none-eabi-objcopy")
set(ARM_OBJDUMP ${ARM_NONE_EABI_OBJDUMP} CACHE FILEPATH "arm-none-eabi-objdump")
