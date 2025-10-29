# examples/sem_basic/CMakeLists.txt

add_executable(sem_basic main.c)

if (TARGET heartos)
    # In-tree build
    target_link_libraries(sem_basic PRIVATE heartos)
    target_include_directories(sem_basic PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    # Out-of-tree (after install) usage
    find_package(HeaRTOS 0.1 REQUIRED)
    target_link_libraries(sem_basic PRIVATE HeaRTOS::heartos)
endif()
