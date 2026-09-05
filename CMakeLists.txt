# HardRT: licensed under the Apache License, Version 2.0
cmake_minimum_required(VERSION 3.16)

set(LIB_NAME "hardrt")
set(LIB_NAME_CPP "hardrtpp")
project(${LIB_NAME} VERSION 0.4.0 LANGUAGES C)

set(HARDRT_PORT "null" CACHE STRING "Port to compile: null|posix|cortex_m")
option(HARDRT_ENABLE_CPP "Build C++ wrappers (header-only interface target)" OFF)
option(HARDRT_BUILD_EXAMPLES "Build examples" ON)
option(HARDRT_BUILD_TESTS "Build test suite (POSIX port)" ON)
option(HARDRT_STALL_ON_ERROR "Stall kernel on fatal error (debug / embedded use)" OFF)
option(HARDRT_DEBUG "Enable debugging and variables" OFF)
option(HARDRT_STRICT "Enable strict warnings on POSIX builds" OFF)
option(HARDRT_SANITIZE "Enable UBSan on POSIX tests" OFF)

# Enable optional languages only when the selected build actually needs them.
# This keeps the C kernel build independent of a host/cross C++ compiler.
if(HARDRT_ENABLE_CPP)
  enable_language(CXX)
endif()
if(HARDRT_PORT STREQUAL "cortex_m")
  enable_language(ASM)
endif()

# Private timing instrumentation. `none` is the production/default profile and
# produces no timing hook code. Additional profiles are enabled only after their
# concrete hook points exist and are validated.
set(HARDRT_TIMING_PROFILE "none" CACHE STRING "Private timing profile: none|ipc")
set_property(CACHE HARDRT_TIMING_PROFILE PROPERTY STRINGS none ipc)
set(HARDRT_TIMING_HOOK_HEADER "" CACHE FILEPATH "Benchmark-provided timing hook header (required for active timing profiles)")

set(HARDRT_TIMING_PROFILE_ID 0)
set(HARDRT_TIMING_ENABLED 0)
set(HARDRT_TIMING_HOOK_HEADER_CMAKE "")
if(HARDRT_TIMING_PROFILE STREQUAL "none")
  # Production/default build: all hook macros resolve to no code.
elseif(HARDRT_TIMING_PROFILE STREQUAL "ipc")
  if(HARDRT_TIMING_HOOK_HEADER STREQUAL "")
    message(FATAL_ERROR "HARDRT_TIMING_PROFILE=ipc requires -DHARDRT_TIMING_HOOK_HEADER=<header>")
  endif()
  if(NOT EXISTS "${HARDRT_TIMING_HOOK_HEADER}")
    message(FATAL_ERROR "HARDRT_TIMING_HOOK_HEADER does not exist: ${HARDRT_TIMING_HOOK_HEADER}")
  endif()
  set(HARDRT_TIMING_PROFILE_ID 1)
  set(HARDRT_TIMING_ENABLED 1)
  file(TO_CMAKE_PATH "${HARDRT_TIMING_HOOK_HEADER}" HARDRT_TIMING_HOOK_HEADER_CMAKE)
else()
  message(FATAL_ERROR "Unknown HARDRT_TIMING_PROFILE=${HARDRT_TIMING_PROFILE}; supported values are none|ipc")
endif()

set(HARDRT_CFG_MAX_TASKS 8 CACHE STRING "Maximum number of concurrent application tasks")
set(HARDRT_CFG_MAX_PRIO 4 CACHE STRING "Number of priority classes (0..N-1; 0 is highest)")

math(EXPR _HRT_CFG_PRIO "${HARDRT_CFG_MAX_PRIO}")
math(EXPR _HRT_CFG_TASKS "${HARDRT_CFG_MAX_TASKS}")
math(EXPR HARDRT_TOTAL_TASKS_VALUE "${HARDRT_CFG_MAX_TASKS} + 1")

if(_HRT_CFG_PRIO LESS 1 OR _HRT_CFG_PRIO GREATER 12)
  message(FATAL_ERROR "HARDRT_CFG_MAX_PRIO must be between 1 and 12 (inclusive). Got ${HARDRT_CFG_MAX_PRIO}.")
endif()
if(_HRT_CFG_TASKS LESS 1)
  message(FATAL_ERROR "HARDRT_CFG_MAX_TASKS must be >= 1. Got ${HARDRT_CFG_MAX_TASKS}.")
endif()
if(_HRT_CFG_TASKS GREATER 254)
  message(FATAL_ERROR "HARDRT_CFG_MAX_TASKS must be <= 254 because task IDs and the private idle slot use uint8_t IDs. Got ${HARDRT_CFG_MAX_TASKS}.")
endif()
if(_HRT_CFG_TASKS LESS _HRT_CFG_PRIO)
  message(FATAL_ERROR "Invalid configuration: HARDRT_CFG_MAX_TASKS (${HARDRT_CFG_MAX_TASKS}) must be >= HARDRT_CFG_MAX_PRIO (${HARDRT_CFG_MAX_PRIO}).")
endif()

set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/inc")
set(SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")
set(SOURCE_CORE_DIR "${SOURCE_DIR}/core")
set(SOURCE_PORT_DIR "${SOURCE_DIR}/port")
set(SOURCE_INTERNAL_DIR "${SOURCE_DIR}/internal")
set(GENERATED_PRIVATE_DIR "${CMAKE_BINARY_DIR}/generated_private")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_PRIVATE_DIR}")

set(HARDRT_PORT_STRING "${HARDRT_PORT}")
if(HARDRT_PORT STREQUAL "null")
  set(HARDRT_PORT_ID 0)
elseif(HARDRT_PORT STREQUAL "posix")
  set(HARDRT_PORT_ID 1)
  if(HARDRT_STALL_ON_ERROR)
    message(STATUS "HARDRT_STALL_ON_ERROR is unsupported on POSIX; forcing effective value OFF")
    set(HARDRT_STALL_ON_ERROR OFF CACHE BOOL
        "Stall kernel on fatal error (debug / embedded use)" FORCE)
  endif()
elseif(HARDRT_PORT STREQUAL "cortex_m")
  set(HARDRT_PORT_ID 2)
else()
  message(FATAL_ERROR "Unknown HARDRT_PORT=${HARDRT_PORT}; supported values are null|posix|cortex_m")
endif()

if(HARDRT_STALL_ON_ERROR)
  set(HARDRT_STALL_ON_ERROR_VALUE 1)
else()
  set(HARDRT_STALL_ON_ERROR_VALUE 0)
endif()
if(HARDRT_DEBUG)
  set(HARDRT_DEBUG_VALUE 1)
else()
  set(HARDRT_DEBUG_VALUE 0)
endif()

message("-- Definitions --")
message("-- HARDRT_PORT                  : ${HARDRT_PORT}")
message("-- HARDRT_STRICT                : ${HARDRT_STRICT}")
message("-- HARDRT_SANITIZE              : ${HARDRT_SANITIZE}")
message("-- HARDRT_ENABLE_CPP            : ${HARDRT_ENABLE_CPP}")
message("-- HARDRT_BUILD_EXAMPLES        : ${HARDRT_BUILD_EXAMPLES}")
message("-- HARDRT_BUILD_TESTS           : ${HARDRT_BUILD_TESTS}")
message("-- HARDRT_STALL_ON_ERROR        : ${HARDRT_STALL_ON_ERROR} (effective)")
message("-- HARDRT_DEBUG                 : ${HARDRT_DEBUG}")
message("-- HARDRT_TIMING_PROFILE        : ${HARDRT_TIMING_PROFILE}")
message("-- HARDRT_CFG_MAX_TASKS         : ${HARDRT_CFG_MAX_TASKS} application tasks")
message("-- HARDRT_TOTAL_TASKS           : ${HARDRT_TOTAL_TASKS_VALUE} including private idle")
message("-- HARDRT_CFG_MAX_PRIO          : ${HARDRT_CFG_MAX_PRIO}")

configure_file(
  "${CMAKE_SOURCE_DIR}/inc/hardrt_port.h.in"
  "${CMAKE_BINARY_DIR}/generated/hardrt_port.h"
  @ONLY
)
configure_file(
  "${CMAKE_SOURCE_DIR}/inc/hardrt_version.h.in"
  "${CMAKE_BINARY_DIR}/generated/hardrt_version.h"
  @ONLY
)
configure_file(
  "${SOURCE_INTERNAL_DIR}/hardrt_timing_config.h.in"
  "${GENERATED_PRIVATE_DIR}/hardrt_timing_config.h"
  @ONLY
)

set(LIBRARY_SOURCES
  "${SOURCE_CORE_DIR}/hardrt_core.c"
  "${SOURCE_CORE_DIR}/hardrt_lifecycle.c"
  "${SOURCE_CORE_DIR}/hardrt_sched.c"
  "${SOURCE_CORE_DIR}/hardrt_time.c"
  "${SOURCE_CORE_DIR}/hardrt_version.c"
  "${SOURCE_CORE_DIR}/hardrt_portinfo.c"
  "${SOURCE_CORE_DIR}/hardrt_sem.c"
  "${SOURCE_CORE_DIR}/hardrt_queue.c"
  "${SOURCE_CORE_DIR}/hardrt_mutex.c"
)

# Keep the existing scheduler implementation private while a small lifecycle
# facade owns the public checked init/create/start API. This avoids duplicating
# scheduler internals merely to add state/configuration validation.
set_source_files_properties("${SOURCE_CORE_DIR}/hardrt_core.c" PROPERTIES
  COMPILE_DEFINITIONS
    "HARDRT_CORE_IMPL;hrt_init=hrt__init_impl;hrt_create_task=hrt__create_task_impl;hrt_start=hrt__start_impl"
)

add_library(${LIB_NAME} STATIC ${LIBRARY_SOURCES})

target_compile_definitions(${LIB_NAME}
  PUBLIC
  HARDRT_APP_MAX_TASKS=${HARDRT_CFG_MAX_TASKS}
  HARDRT_MAX_TASKS=${HARDRT_TOTAL_TASKS_VALUE}
  HARDRT_MAX_PRIO=${HARDRT_CFG_MAX_PRIO}
  HARDRT_STALL_ON_ERROR=${HARDRT_STALL_ON_ERROR_VALUE}
  HARDRT_DEBUG=${HARDRT_DEBUG_VALUE}
)

target_include_directories(${LIB_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${INCLUDE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:include>
  PRIVATE
  ${SOURCE_INTERNAL_DIR}
  ${GENERATED_PRIVATE_DIR}
)
target_compile_features(${LIB_NAME} PUBLIC c_std_11)
target_compile_options(${LIB_NAME} PRIVATE -ffreestanding -fno-strict-aliasing)

if(HARDRT_PORT STREQUAL "posix" AND HARDRT_STRICT)
  message(STATUS "HARDRT_STRICT enabled (POSIX warnings on)")
  set(HARDRT_STRICT_WARNINGS
    -Wall -Wextra -Wpedantic -Wconversion -Wcast-qual -Wshadow
  )
  add_compile_options(${HARDRT_STRICT_WARNINGS})
endif()

set_target_properties(${LIB_NAME} PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  OUTPUT_NAME ${LIB_NAME}
)

if(HARDRT_PORT STREQUAL "null")
  target_sources(${LIB_NAME} PRIVATE "${SOURCE_PORT_DIR}/null/port_null.c")
elseif(HARDRT_PORT STREQUAL "posix")
  target_sources(${LIB_NAME} PRIVATE "${SOURCE_PORT_DIR}/posix/port_posix.c")
elseif(HARDRT_PORT STREQUAL "cortex_m")
  target_sources(${LIB_NAME} PRIVATE
    "${SOURCE_PORT_DIR}/cortex_m/port_cortexm.c"
    "${SOURCE_PORT_DIR}/cortex_m/port_cortexm_isr.c"
    "${SOURCE_PORT_DIR}/cortex_m/hardfault_diag.c"
    "${SOURCE_PORT_DIR}/cortex_m/hrt_pendsv_handler.s"
  )
endif()

if(HARDRT_ENABLE_CPP)
  add_library(${LIB_NAME_CPP} INTERFACE)
  target_link_libraries(${LIB_NAME_CPP} INTERFACE ${LIB_NAME})
  target_compile_features(${LIB_NAME_CPP} INTERFACE cxx_std_17)
  target_include_directories(${LIB_NAME_CPP} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/cpp>
    $<BUILD_INTERFACE:${INCLUDE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
  )
endif()

include(GNUInstallDirs)

install(TARGETS ${LIB_NAME}
  EXPORT HardRTTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(HARDRT_ENABLE_CPP)
  install(TARGETS ${LIB_NAME_CPP}
    EXPORT HardRTTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

install(DIRECTORY ${INCLUDE_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h"
  PATTERN "hardrt_port_int.h" EXCLUDE)
install(DIRECTORY "${CMAKE_BINARY_DIR}/generated/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(HARDRT_ENABLE_CPP)
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/cpp/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/HardRTConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/cmake/HardRTConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/HardRTConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HardRT
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/HardRTConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/HardRTConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HardRT
)
install(EXPORT HardRTTargets
  NAMESPACE HardRT::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HardRT
)

if(HARDRT_BUILD_EXAMPLES)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/two_tasks)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/queue_posix)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/sem_basic)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/mutex_basic)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/sem_counting)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/two_tasks_external)
  if(HARDRT_ENABLE_CPP)
    add_subdirectory(${CMAKE_SOURCE_DIR}/examples/two_tasks_cpp)
    add_subdirectory(${CMAKE_SOURCE_DIR}/examples/queue_posix_cpp)
    add_subdirectory(${CMAKE_SOURCE_DIR}/examples/sem_basic_cpp)
    add_subdirectory(${CMAKE_SOURCE_DIR}/examples/mutex_basic_cpp)
    add_subdirectory(${CMAKE_SOURCE_DIR}/examples/sem_counting_cpp)
  endif()
endif()

if(HARDRT_BUILD_TESTS)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/hardrt_tester.cmake)

  add_library(hardrt_port_contract_fixture OBJECT tests/port_contract_fixture.c)
  target_link_libraries(hardrt_port_contract_fixture PRIVATE ${LIB_NAME})
  target_include_directories(hardrt_port_contract_fixture PRIVATE ${SOURCE_INTERNAL_DIR})
  target_compile_features(hardrt_port_contract_fixture PRIVATE c_std_11)
endif()
