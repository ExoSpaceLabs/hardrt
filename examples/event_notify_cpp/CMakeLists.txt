add_executable(event_notify_cpp main.cpp)

if (TARGET hardrtpp)
    target_link_libraries(event_notify_cpp PRIVATE hardrtpp)
    target_include_directories(event_notify_cpp PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
else()
    find_package(HardRT 0.3 REQUIRED)
    target_link_libraries(event_notify_cpp PRIVATE HardRT::hardrtpp)
endif()
