# HardRT: licensed under the Apache License, Version 2.0
cmake_minimum_required(VERSION 3.16)

# ---- Project ----
set(LIB_NAME "hardrt")
set(LIB_NAME_CPP "hardrtpp")
project(${LIB_NAME} VERSION 0.3.0 LANGUAGES C CXX ASM)

# ---- Options ----
set(HARDRT_PORT "null" CACHE STRING "Port to compile: null|posix|cortex_m")
option(HARDRT_ENABLE_CPP "Build C++ wrappers (header-only interface target)" OFF)
option(HARDRT_BUILD_EXAMPLES "Build examples" ON)
option(HARDRT_BUILD_TESTS "Build test suite (POSIX port)" ON)
option(HARDRT_STALL_ON_ERROR "Stall kernel on fatal error (debug / embedded use)" OFF)

# ---- Kernel sizing knobs (public compile definitions) ----
# These control the number of concurrent tasks and the number of priority classes.
# They are wired into the public headers via HARDRT_MAX_TASKS/HARDRT_MAX_PRIO.
set(HARDRT_CFG_MAX_TASKS 8 CACHE STRING "Maximum number of concurrent tasks")
set(HARDRT_CFG_MAX_PRIO  4 CACHE STRING "Number of priority classes (0..N-1; 0 is highest)")


message("-- Definitions --")
message("-- HARDRT_PORT                  : ${HARDRT_PORT}")
message("-- HARDRT_ENABLE_CPP            : ${HARDRT_ENABLE_CPP}")
message("-- HARDRT_BUILD_EXAMPLES        : ${HARDRT_BUILD_EXAMPLES}")
message("-- HARDRT_BUILD_TESTS           : ${HARDRT_BUILD_TESTS}")
message("-- HARDRT_STALL_ON_ERROR        : ${HARDRT_STALL_ON_ERROR}")
message("-- HARDRT_CFG_MAX_TASKS         : ${HARDRT_CFG_MAX_TASKS}")
message("-- HARDRT_CFG_MAX_PRIO          : ${HARDRT_CFG_MAX_PRIO}")

# Validate sizing knobs at configure time (no kernel source changes needed)
# Constraints:
# - 1 <= HARDRT_CFG_MAX_PRIO <= 12 (physical limit in current enum)
# - HARDRT_CFG_MAX_TASKS >= 1
# - HARDRT_CFG_MAX_TASKS >= HARDRT_CFG_MAX_PRIO
math(EXPR _HRT_CFG_PRIO "${HARDRT_CFG_MAX_PRIO}")
math(EXPR _HRT_CFG_TASKS "${HARDRT_CFG_MAX_TASKS}")

if(_HRT_CFG_PRIO LESS 1 OR _HRT_CFG_PRIO GREATER 12)
  message(FATAL_ERROR "HARDRT_CFG_MAX_PRIO must be between 1 and 12 (inclusive). Got ${HARDRT_CFG_MAX_PRIO}.")
endif()
if(_HRT_CFG_TASKS LESS 1)
  message(FATAL_ERROR "HARDRT_CFG_MAX_TASKS must be >= 1. Got ${HARDRT_CFG_MAX_TASKS}.")
endif()
if(_HRT_CFG_TASKS LESS _HRT_CFG_PRIO)
  message(FATAL_ERROR "Invalid configuration: HARDRT_CFG_MAX_TASKS (${HARDRT_CFG_MAX_TASKS}) must be >= HARDRT_CFG_MAX_PRIO (${HARDRT_CFG_MAX_PRIO}).")
endif()

# ---- Paths ----
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/inc")
set(SOURCE_DIR  "${CMAKE_SOURCE_DIR}/src")
set(SOURCE_CORE_DIR "${SOURCE_DIR}/core")
set(SOURCE_PORT_DIR "${SOURCE_DIR}/port")

# ---- Generated headers (version + port identity) ----
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

# helper vars so the @HARDRT_PORT@_STR_EQ_* trick in hardrt_port.h.in works
# ---- Port ID/name for generated header ----
set(HARDRT_PORT_STRING "${HARDRT_PORT}")
if(HARDRT_PORT STREQUAL "null")
  set(HARDRT_PORT_ID 0)
elseif(HARDRT_PORT STREQUAL "posix")
  set(HARDRT_PORT_ID 1)
  if(HARDRT_STALL_ON_ERROR)
    message(WARNING "HRT_STALL_ON_ERROR is disabled for posix port.")
    set(HRT_STALL_ON_ERROR 0)
  endif()
elseif(HARDRT_PORT STREQUAL "cortex_m")
  set(HARDRT_PORT_ID 2)
else()
  set(HARDRT_PORT_ID -1)
endif()

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
        "${CMAKE_SOURCE_DIR}/inc/hardrt_port.h.in"
        "${CMAKE_BINARY_DIR}/generated/hardrt_port.h"
        @ONLY
)

# ---- Library sources ----
set(LIBRARY_SOURCES
        "${SOURCE_CORE_DIR}/hardrt_core.c"
        "${SOURCE_CORE_DIR}/hardrt_sched.c"
        "${SOURCE_CORE_DIR}/hardrt_time.c"
        "${SOURCE_CORE_DIR}/hardrt_version.c"
        "${SOURCE_CORE_DIR}/hardrt_portinfo.c"   # provides hrt_port_name()/hrt_port_id()
        "${SOURCE_CORE_DIR}/hardrt_sem.c"
)

# ---- Library target ----
add_library(${LIB_NAME} STATIC ${LIBRARY_SOURCES})

# Propagate sizing knobs to all consumers (library PUBLIC interface)
# so examples/tests and downstream apps see consistent limits.
target_compile_definitions(${LIB_NAME}
        PUBLIC
        HARDRT_MAX_TASKS=${HARDRT_CFG_MAX_TASKS}
        HARDRT_MAX_PRIO=${HARDRT_CFG_MAX_PRIO}
        HRT_STALL_ON_ERROR=${HRT_STALL_ON_ERROR}
)

target_include_directories(${LIB_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${INCLUDE_DIR}    # so ports can include hardrt_port_int.h
)
target_compile_features(${LIB_NAME} PUBLIC c_std_11)
target_compile_options(${LIB_NAME} PRIVATE -ffreestanding -fno-strict-aliasing)

# Mark library version (handy if you ever switch to SHARED)
set_target_properties(${LIB_NAME} PROPERTIES
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME ${LIB_NAME}
)

# ---- Select port (device-agnostic here) ----
if(HARDRT_PORT STREQUAL "null")
  target_sources(${LIB_NAME} PRIVATE "${SOURCE_PORT_DIR}/null/port_null.c")
elseif(HARDRT_PORT STREQUAL "posix")
  target_sources(${LIB_NAME} PRIVATE "${SOURCE_PORT_DIR}/posix/port_posix.c")
  # setitimer/nanosleep are in libc; no extra link flags required on glibc
elseif(HARDRT_PORT STREQUAL "cortex_m")
  target_sources(${LIB_NAME} PRIVATE
          "${SOURCE_PORT_DIR}/cortex_m/port_cortexm.c"
          "${SOURCE_PORT_DIR}/cortex_m/port_cortexm_isr.c"
          "${SOURCE_PORT_DIR}/cortex_m/hardfault_diag.c"
          "${SOURCE_PORT_DIR}/cortex_m/hrt_pendsv_handler.s"
  )
else()
  message(FATAL_ERROR "Unknown HARDRT_PORT=${HARDRT_PORT}")
endif()

# ---- Optional C++ wrappers (header-only) ----
if(HARDRT_ENABLE_CPP)
  add_library(${LIB_NAME_CPP} INTERFACE)
  target_sources(${LIB_NAME_CPP} INTERFACE cpp/hardrtpp.hpp)
  target_link_libraries(${LIB_NAME_CPP} INTERFACE ${LIB_NAME})
  target_compile_features(${LIB_NAME_CPP} INTERFACE cxx_std_17)
  target_include_directories(${LIB_NAME_CPP} INTERFACE
          $<BUILD_INTERFACE:${INCLUDE_DIR}>
          $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
          $<INSTALL_INTERFACE:include>
  )
endif()

# ---- Install / export package (find_package support) ----
include(GNUInstallDirs)

install(TARGETS ${LIB_NAME}
        EXPORT HardRTTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# public + generated headers

# Install only public headers; exclude the internal one
install(DIRECTORY ${INCLUDE_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
        PATTERN "hardrt_port_int.h" EXCLUDE)

install(DIRECTORY "${CMAKE_BINARY_DIR}/generated/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Package config
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

# ---- Examples ----
if(HARDRT_BUILD_EXAMPLES)
  # Examples have their own CMakeLists.txt; include them directly
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/two_tasks)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/sem_basic)
  add_subdirectory(${CMAKE_SOURCE_DIR}/examples/two_tasks_external)
endif()

# ---- Tests (POSIX only) ----
if(HARDRT_BUILD_TESTS)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/hardrt_tester.cmake)
endif()
