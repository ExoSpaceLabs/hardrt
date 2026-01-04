# examples/two_tasks/CMakeLists.txt

add_executable(two_tasks main.c
        ../../inc/hardrt_sem.h)

if (TARGET hardrt)
    # In-tree build
    target_link_libraries(two_tasks PRIVATE hardrt)
    target_include_directories(two_tasks PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    # Out-of-tree (after install) usage
    find_package(HardRT 0.3 REQUIRED)
    target_link_libraries(two_tasks PRIVATE HardRT::hardrt)
endif()
