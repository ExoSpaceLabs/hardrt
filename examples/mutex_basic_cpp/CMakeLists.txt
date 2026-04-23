add_executable(mutex_basic_cpp main.cpp)

if(TARGET hardrtpp)
    target_link_libraries(mutex_basic_cpp PRIVATE hardrtpp)
    target_include_directories(mutex_basic_cpp PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
endif()

if(HardRT_FOUND)
    target_link_libraries(mutex_basic_cpp PRIVATE HardRT::hardrtpp)
endif()
