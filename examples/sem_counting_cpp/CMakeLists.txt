add_executable(sem_counting_cpp main.cpp)

if (TARGET hardrtpp)
    target_link_libraries(sem_counting_cpp PRIVATE hardrtpp)
    target_include_directories(sem_counting_cpp PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
else()
    find_package(HardRT REQUIRED)
    target_link_libraries(sem_counting_cpp PRIVATE HardRT::hardrtpp)
endif()
