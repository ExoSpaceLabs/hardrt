# HeaRTOS: licensed under the Apache License, Version 2.0
cmake_minimum_required(VERSION 3.16)

# ---- Project ----
set(LIB_NAME "heartos")
set(LIB_NAME_CPP "heartospp")
project(${LIB_NAME} VERSION 0.2.0 LANGUAGES C CXX)

# ---- Options ----
set(HEARTOS_PORT "null" CACHE STRING "Port to compile: null|posix|cortex_m")
option(HEARTOS_ENABLE_CPP "Build C++ wrappers (header-only interface target)" OFF)
option(HEARTOS_BUILD_EXAMPLES "Build examples" ON)

# ---- Paths ----
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/inc")
set(SOURCE_DIR  "${CMAKE_SOURCE_DIR}/src")
set(SOURCE_CORE_DIR "${SOURCE_DIR}/core")
set(SOURCE_PORT_DIR "${SOURCE_DIR}/port")

# ---- Generated headers (version + port identity) ----
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

# helper vars so the @HEARTOS_PORT@_STR_EQ_* trick in heartos_port.h.in works
# ---- Port ID/name for generated header ----
set(HEARTOS_PORT_STRING "${HEARTOS_PORT}")
if(HEARTOS_PORT STREQUAL "null")
  set(HEARTOS_PORT_ID 0)
elseif(HEARTOS_PORT STREQUAL "posix")
  set(HEARTOS_PORT_ID 1)
elseif(HEARTOS_PORT STREQUAL "cortex_m")
  set(HEARTOS_PORT_ID 2)
else()
  set(HEARTOS_PORT_ID -1)
endif()

configure_file(
        "${CMAKE_SOURCE_DIR}/inc/heartos_port.h.in"
        "${CMAKE_BINARY_DIR}/generated/heartos_port.h"
        @ONLY
)


configure_file(
        "${CMAKE_SOURCE_DIR}/inc/heartos_version.h.in"
        "${CMAKE_BINARY_DIR}/generated/heartos_version.h"
        @ONLY
)
configure_file(
        "${CMAKE_SOURCE_DIR}/inc/heartos_port.h.in"
        "${CMAKE_BINARY_DIR}/generated/heartos_port.h"
        @ONLY
)

# ---- Library sources ----
set(LIBRARY_SOURCES
        "${SOURCE_CORE_DIR}/heartos_core.c"
        "${SOURCE_CORE_DIR}/heartos_sched.c"
        "${SOURCE_CORE_DIR}/heartos_time.c"
        "${SOURCE_CORE_DIR}/heartos_version.c"
        "${SOURCE_CORE_DIR}/heartos_portinfo.c"   # provides hrt_port_name()/hrt_port_id()
)

# ---- Library target ----
add_library(${LIB_NAME} STATIC ${LIBRARY_SOURCES})

target_include_directories(${LIB_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
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
if(HEARTOS_PORT STREQUAL "null")
  target_sources(${LIB_NAME} PRIVATE "${SOURCE_PORT_DIR}/null/port_null.c")
elseif(HEARTOS_PORT STREQUAL "posix")
  target_sources(${LIB_NAME} PRIVATE "${SOURCE_PORT_DIR}/posix/port_posix.c")
  # setitimer/nanosleep are in libc; no extra link flags required on glibc
elseif(HEARTOS_PORT STREQUAL "cortex_m")
  message(FATAL_ERROR "Cortex-M port not implemented yet. Set HEARTOS_PORT=null or posix.")
else()
  message(FATAL_ERROR "Unknown HEARTOS_PORT=${HEARTOS_PORT}")
endif()

# ---- Optional C++ wrappers (header-only) ----
if(HEARTOS_ENABLE_CPP)
  add_library(${LIB_NAME_CPP} INTERFACE)
  target_sources(${LIB_NAME_CPP} INTERFACE cpp/heartospp.hpp)
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
        EXPORT HeaRTOSTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# public + generated headers
install(DIRECTORY ${INCLUDE_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${CMAKE_BINARY_DIR}/generated/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Package config
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/HeaRTOSConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
        "${CMAKE_CURRENT_LIST_DIR}/cmake/HeaRTOSConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/HeaRTOSConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HeaRTOS
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/HeaRTOSConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/HeaRTOSConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HeaRTOS
)
install(EXPORT HeaRTOSTargets
        NAMESPACE HeaRTOS::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HeaRTOS
)

# ---- Examples ----
if(HEARTOS_BUILD_EXAMPLES)
  add_subdirectory(examples/two_tasks)
endif()
