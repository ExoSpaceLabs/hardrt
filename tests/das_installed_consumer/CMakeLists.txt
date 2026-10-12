cmake_minimum_required(VERSION 3.20)
project(hardrt_das_release_consumer LANGUAGES C)

if(NOT DAS_SOURCE_ROOT OR NOT EXISTS "${DAS_SOURCE_ROOT}/examples/hardrt_uart/main.c")
  message(FATAL_ERROR "DAS_SOURCE_ROOT must contain verified release example")
endif()
if(NOT HARDRT_EXPECTED_VERSION)
  message(FATAL_ERROR "Set HARDRT_EXPECTED_VERSION from root VERSION")
endif()

set(DAS_USE_DEFAULT_VECTOR_TABLE ON)
find_package(DAS 0.1.0 EXACT CONFIG REQUIRED)
find_package(HardRT ${HARDRT_EXPECTED_VERSION} EXACT CONFIG REQUIRED)
if(NOT DAS_PACKAGE_CORE STREQUAL "cm7")
  message(FATAL_ERROR "Expected the published DAS STM32 CM7 package")
endif()
if(NOT DAS_VERSION STREQUAL "0.1.0")
  message(FATAL_ERROR "Unexpected installed DAS version: ${DAS_VERSION}")
endif()

add_executable(hardrt_das_release_consumer
  "${DAS_SOURCE_ROOT}/examples/hardrt_uart/main.c")
target_link_libraries(hardrt_das_release_consumer PRIVATE HardRT::hardrt das::das)
target_compile_features(hardrt_das_release_consumer PRIVATE c_std_11)
target_compile_options(hardrt_das_release_consumer PRIVATE
  -Wall -Wextra -Werror -ffreestanding -fno-builtin)
target_link_options(hardrt_das_release_consumer PRIVATE
  -nostartfiles --specs=nosys.specs -Wl,--gc-sections
  -Wl,--unresolved-symbols=report-all
  -Wl,-u,HardFault_Handler -Wl,-u,PendSV_Handler -Wl,-u,SysTick_Handler)
set_target_properties(hardrt_das_release_consumer PROPERTIES SUFFIX ".elf")
