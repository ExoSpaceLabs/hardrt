add_executable(sem_basic_cpp main.cpp)

if (TARGET hardrtpp)
    target_link_libraries(sem_basic_cpp PRIVATE hardrtpp)
    target_include_directories(sem_basic_cpp PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
else()
    find_package(HardRT REQUIRED)
    target_link_libraries(sem_basic_cpp PRIVATE HardRT::hardrtpp)
endif()
