add_executable(two_tasks main.c)
target_link_libraries(two_tasks PRIVATE heartos)
target_include_directories(two_tasks PRIVATE ${CMAKE_SOURCE_DIR}/inc)