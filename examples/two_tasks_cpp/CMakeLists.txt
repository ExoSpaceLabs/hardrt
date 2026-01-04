add_executable(two_tasks_cpp main.cpp)

if (TARGET hardrtpp)
    target_link_libraries(two_tasks_cpp PRIVATE hardrtpp)
    target_include_directories(two_tasks_cpp PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
else()
    # If the user is building this example standalone, they'd need to find the package.
    # But here we are likely in-tree.
    find_package(HardRT REQUIRED)
    target_link_libraries(two_tasks_cpp PRIVATE HardRT::hardrtpp)
endif()
