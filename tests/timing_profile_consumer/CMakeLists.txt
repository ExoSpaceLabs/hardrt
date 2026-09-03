cmake_minimum_required(VERSION 3.16)
project(hardrt_timing_profile_consumer LANGUAGES C)

find_package(HardRT CONFIG REQUIRED)

add_executable(timing_profile_smoke main.c)
target_link_libraries(timing_profile_smoke PRIVATE HardRT::hardrt)
target_compile_features(timing_profile_smoke PRIVATE c_std_11)
