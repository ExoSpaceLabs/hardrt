cmake_minimum_required(VERSION 3.16)
project(hardrt_installed_consumer LANGUAGES C)

find_package(HardRT 0.4.0 REQUIRED)

add_executable(hardrt_installed_consumer main.c)
target_link_libraries(hardrt_installed_consumer PRIVATE HardRT::hardrt)
target_compile_features(hardrt_installed_consumer PRIVATE c_std_11)
