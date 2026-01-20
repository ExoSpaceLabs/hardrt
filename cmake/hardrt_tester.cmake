# HardRT test targets (included only when HARDRT_BUILD_TESTS is ON)

# Enable CTest for this directory
enable_testing()

# Only the POSIX port has a runnable scheduler and tests for now
if(HARDRT_PORT STREQUAL "posix")
  # Expose test hooks inside the library for POSIX scheduler to stop cleanly
  target_compile_definitions(${LIB_NAME} PRIVATE HARDRT_TEST_HOOKS)

  add_executable(hardrt_tests
          ${CMAKE_CURRENT_LIST_DIR}/../tests/test_main.c # path fixed below via source-dir; keep list readable
          )
  # Replace the single source above with the actual list relative to project root
  set_target_properties(hardrt_tests PROPERTIES LINKER_LANGUAGE C)
  target_sources(hardrt_tests PRIVATE
          ${CMAKE_SOURCE_DIR}/tests/test_main.c
          ${CMAKE_SOURCE_DIR}/tests/test_identity.c
          ${CMAKE_SOURCE_DIR}/tests/test_sleep_stop.c
          ${CMAKE_SOURCE_DIR}/tests/test_rr_yield.c
          ${CMAKE_SOURCE_DIR}/tests/test_rr_sleep.c
          ${CMAKE_SOURCE_DIR}/tests/test_priority.c
          ${CMAKE_SOURCE_DIR}/tests/test_coop_vs_rr.c
          ${CMAKE_SOURCE_DIR}/tests/test_tick_rate.c
          ${CMAKE_SOURCE_DIR}/tests/test_create_limits.c
          ${CMAKE_SOURCE_DIR}/tests/test_runtime_tuning.c
          ${CMAKE_SOURCE_DIR}/tests/test_fifo_order.c
          ${CMAKE_SOURCE_DIR}/tests/test_wraparound.c
          ${CMAKE_SOURCE_DIR}/tests/test_sleep_zero.c
          ${CMAKE_SOURCE_DIR}/tests/test_task_return.c
          ${CMAKE_SOURCE_DIR}/tests/test_semaphore.c
          ${CMAKE_SOURCE_DIR}/tests/test_queue.c
          ${CMAKE_SOURCE_DIR}/tests/test_external_tick.c
          ${CMAKE_SOURCE_DIR}/tests/test_idle_behavior.c
  )

  target_link_libraries(hardrt_tests PRIVATE ${LIB_NAME})
  target_compile_features(hardrt_tests PRIVATE c_std_11)
  # Ensure test sources also see HARDRT_TEST_HOOKS to enable hook-dependent cases
  target_compile_definitions(hardrt_tests PRIVATE HARDRT_TEST_HOOKS)
  if(HARDRT_SANITIZE)
    # UBSan is generally fine
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined -fno-omit-frame-pointer)

    message(STATUS "POSIX sanitizers enabled: UBSan")
    message(STATUS "ASan disabled: ucontext (makecontext/swapcontext) is not ASan-safe")
  endif()
  add_test(NAME hardrt_tests COMMAND hardrt_tests)
else()
  message(STATUS "Tests are enabled but HARDRT_PORT=${HARDRT_PORT} has no runtime scheduler; skipping test target")
endif()