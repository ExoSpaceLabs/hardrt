# examples/two_tasks/CMakeLists.txt

add_executable(two_tasks main.c
        ../../inc/heartos_sem.h)

if (TARGET heartos)
    # In-tree build
    target_link_libraries(two_tasks PRIVATE heartos)
    target_include_directories(two_tasks PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    # Out-of-tree (after install) usage
    find_package(HeaRTOS 0.1 REQUIRED)
    target_link_libraries(two_tasks PRIVATE HeaRTOS::heartos)
endif()
