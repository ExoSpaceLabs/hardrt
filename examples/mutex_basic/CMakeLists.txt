# examples/mutex_basic/CMakeLists.txt

add_executable(mutex_basic main.c)

# If consuming internally during development:
if(TARGET hardrt)
    target_link_libraries(mutex_basic PRIVATE hardrt)
    target_include_directories(mutex_basic PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
endif()

# If using as an installed package:
if(HardRT_FOUND)
    target_link_libraries(mutex_basic PRIVATE HardRT::hardrt)
endif()
