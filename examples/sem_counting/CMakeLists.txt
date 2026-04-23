# examples/sem_counting/CMakeLists.txt

add_executable(sem_counting main.c)

if (TARGET hardrt)
    # In-tree build
    target_link_libraries(sem_counting PRIVATE hardrt)
    target_include_directories(sem_counting PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    # Out-of-tree (after install) usage
    find_package(HardRT 0.3 REQUIRED)
    target_link_libraries(sem_counting PRIVATE HardRT::hardrt)
endif()
