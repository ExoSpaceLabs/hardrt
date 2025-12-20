# examples/two_tasks_external/CMakeLists.txt

add_executable(two_tasks_external main.c)

# POSIX tick thread uses pthreads
find_package(Threads REQUIRED)

if (TARGET hardrt)
    # In-tree build
    target_link_libraries(two_tasks_external PRIVATE hardrt Threads::Threads)
    target_include_directories(two_tasks_external PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    # Out-of-tree (after install) usage
    find_package(HardRT 0.1 REQUIRED)
    target_link_libraries(two_tasks_external PRIVATE HardRT::hardrt Threads::Threads)
endif()
