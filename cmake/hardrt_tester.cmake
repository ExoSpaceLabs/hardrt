# HardRT test targets (included only when HARDRT_BUILD_TESTS is ON)

enable_testing()

if(HARDRT_PORT STREQUAL "posix")
  target_compile_definitions(${LIB_NAME} PRIVATE HARDRT_TEST_HOOKS)
  find_package(Threads REQUIRED)

  add_executable(hardrt_tests
          ${CMAKE_CURRENT_LIST_DIR}/../tests/test_main.c
          )
  set_target_properties(hardrt_tests PROPERTIES LINKER_LANGUAGE C)
  target_sources(hardrt_tests PRIVATE
          ${CMAKE_SOURCE_DIR}/tests/test_main.c
          ${CMAKE_SOURCE_DIR}/tests/test_identity.c
          ${CMAKE_SOURCE_DIR}/tests/test_sleep_stop.c
          ${CMAKE_SOURCE_DIR}/tests/test_rr_yield.c
          ${CMAKE_SOURCE_DIR}/tests/test_rr_sleep.c
          ${CMAKE_SOURCE_DIR}/tests/test_priority.c
          ${CMAKE_SOURCE_DIR}/tests/test_preemption_contract.c
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
          ${CMAKE_SOURCE_DIR}/tests/test_mutex.c
          ${CMAKE_SOURCE_DIR}/tests/test_now_ms.c
  )

  target_link_libraries(hardrt_tests PRIVATE ${LIB_NAME} Threads::Threads)
  target_compile_features(hardrt_tests PRIVATE c_std_11)
  target_compile_definitions(hardrt_tests PRIVATE HARDRT_TEST_HOOKS)
  if(HARDRT_SANITIZE)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined -fno-omit-frame-pointer)

    message(STATUS "POSIX sanitizers enabled: UBSan")
    message(STATUS "ASan disabled: ucontext (makecontext/swapcontext) is not ASan-safe")
  endif()
  add_test(NAME hardrt_tests COMMAND hardrt_tests)
else()
  message(STATUS "Tests are enabled but HARDRT_PORT=${HARDRT_PORT} has no runtime scheduler; skipping test target")
endif()
