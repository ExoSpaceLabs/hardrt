# examples/two_tasks_external/CMakeLists.txt

add_executable(two_tasks_external main.c)

# POSIX tick thread uses pthreads
find_package(Threads REQUIRED)

if (TARGET heartos)
    # In-tree build
    target_link_libraries(two_tasks_external PRIVATE heartos Threads::Threads)
    target_include_directories(two_tasks_external PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    # Out-of-tree (after install) usage
    find_package(HeaRTOS 0.1 REQUIRED)
    target_link_libraries(two_tasks_external PRIVATE HeaRTOS::heartos Threads::Threads)
endif()
