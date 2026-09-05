add_executable(event_notify main.c)

if (TARGET hardrt)
    target_link_libraries(event_notify PRIVATE hardrt)
    target_include_directories(event_notify PRIVATE ${CMAKE_SOURCE_DIR}/inc ${CMAKE_BINARY_DIR}/generated)
else()
    find_package(HardRT 0.3 REQUIRED)
    target_link_libraries(event_notify PRIVATE HardRT::hardrt)
endif()
