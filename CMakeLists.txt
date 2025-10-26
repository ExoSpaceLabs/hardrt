# HeaRTOS: licensed under the Apache License, Version 2.0

cmake_minimum_required(VERSION 3.16)
project(heartos LANGUAGES C)

# Options
set(HEARTOS_PORT "null" CACHE STRING "Port to compile: null|posix|cortex_m")
option(HEARTOS_ENABLE_CPP "Build C++ wrappers (header-only interface target)" OFF)
option(HEARTOS_BUILD_EXAMPLES "Build examples" ON)

# Library
add_library(heartos STATIC
        src/core/heartos_core.c
        src/core/heartos_sched.c
        src/core/heartos_time.c
)

target_include_directories(heartos PUBLIC inc)
target_compile_features(heartos PUBLIC c_std_11)
target_compile_options(heartos PRIVATE -ffreestanding -fno-strict-aliasing)

# Select port source (kept device-agnostic)
if(HEARTOS_PORT STREQUAL "null")
  target_sources(heartos PRIVATE src/port/null/port_null.c)
elseif(HEARTOS_PORT STREQUAL "posix")
  # You’ll add src/port/posix/rtos_port_posix.c later
  message(FATAL_ERROR "POSIX port not implemented yet. Set HEARTOS_PORT=null for now.")
elseif(HEARTOS_PORT STREQUAL "cortex_m")
  # You’ll add src/port/cortex_m/port_cm.c later
  message(FATAL_ERROR "Cortex-M port not implemented yet. Set HEARTOS_PORT=null for now.")
else()
  message(FATAL_ERROR "Unknown HEARTOS_PORT=${HEARTOS_PORT}")
endif()

# Optional C++ wrappers: header-only target
if(HEARTOS_ENABLE_CPP)
  enable_language(CXX)
  add_library(heartospp INTERFACE)
  target_sources(heartospp INTERFACE cpp/heartospp.hpp)
  target_link_libraries(heartospp INTERFACE heartos)
  target_compile_features(heartospp INTERFACE cxx_std_17)
  target_include_directories(heartospp INTERFACE inc)
endif()

# Examples
if(HEARTOS_BUILD_EXAMPLES)
  add_subdirectory(examples/two_tasks)
endif()