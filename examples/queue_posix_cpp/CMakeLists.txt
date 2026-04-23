add_executable(queue_posix_cpp main.cpp)

if (TARGET hardrtpp)
    target_link_libraries(queue_posix_cpp PRIVATE hardrtpp)
    target_include_directories(queue_posix_cpp PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
else()
    find_package(HardRT REQUIRED)
    target_link_libraries(queue_posix_cpp PRIVATE HardRT::hardrtpp)
endif()
