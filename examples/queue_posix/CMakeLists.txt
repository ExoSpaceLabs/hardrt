# examples/queue_posix/CMakeLists.txt

add_executable(queue_posix main.c
        ../../inc/hardrt_queue.h)

if (TARGET hardrt)
    # In-tree build
    target_link_libraries(queue_posix PRIVATE hardrt)
    target_include_directories(queue_posix PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    # Out-of-tree (after install) usage
    find_package(HardRT 0.3 REQUIRED)
    target_link_libraries(queue_posix PRIVATE HardRT::hardrt)
endif()
