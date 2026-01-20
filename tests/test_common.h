#ifndef HARDRT_TEST_COMMON_H
#define HARDRT_TEST_COMMON_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Public API under test */
#include "hardrt.h"

#ifdef __cplusplus
extern "C" {

#endif

/**
 * @brief Stop the scheduler loop (test-only hook; POSIX port).
 */
void hrt__test_stop_scheduler(void);

/**
 * @brief Reset scheduler internal state to defaults (test-only; POSIX port).
 */
void hrt__test_reset_scheduler_state(void);

/* Optional extra hooks (only available when HARDRT_TEST_HOOKS) */
#ifdef HARDRT_TEST_HOOKS
/**
 * @brief Reset the idle loop counter.
 */
void hrt__test_idle_counter_reset(void);

/**
 * @brief Read the idle loop counter value.
 */
unsigned long long hrt__test_idle_counter_value(void);

/**
 * @brief Advance the system tick by a delta (no real time passes).
 */
void hrt__test_fast_forward_ticks(uint32_t delta);

/**
 * @brief Set the tick counter to an exact value.
 */
void hrt__test_set_tick(uint32_t v);

/**
 * @brief Get the current tick counter.
 */
uint32_t hrt__test_get_tick(void);
#endif
#ifdef __cplusplus
}
#endif

/* Color helpers */
#define ANSI_GRN "\x1b[32m"
#define ANSI_RED "\x1b[31m"
#define ANSI_RST "\x1b[0m"

/* Global failure counter owned by test_main.c */
extern int g_failures;

/* Tiny assertion macros printing to stdout (keeps CTest output single-stream) */
#define T_ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        ++g_failures; \
        printf(ANSI_RED "FAIL" ANSI_RST ": %s\n", msg); \
        printf("  ASSERT: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    } else { \
        printf(ANSI_GRN "PASS" ANSI_RST ": %s\n", msg); \
    } \
} while(0)

#define T_ASSERT_EQ_INT(exp, got, msg) do { \
    if ((exp) != (got)) { \
        ++g_failures; \
        printf(ANSI_RED "FAIL" ANSI_RST ": %s\n", msg); \
        printf("  expected %d, got %d (%s:%d)\n", (int)(exp), (int)(got), __FILE__, __LINE__); \
    } else { \
        printf(ANSI_GRN "PASS" ANSI_RST ": %s (=%d)\n", msg, (int)(got)); \
    } \
} while(0)

#define T_ASSERT_STREQ(exp, got, msg) do { \
    if (strcmp((exp),(got)) != 0) { \
        ++g_failures; \
        printf(ANSI_RED "FAIL" ANSI_RST ": %s\n", msg); \
        printf("  expected '%s', got '%s' (%s:%d)\n", (exp), (got), __FILE__, __LINE__); \
    } else { \
        printf(ANSI_GRN "PASS" ANSI_RST ": %s ('%s')\n", msg, (got)); \
    } \
} while(0)

/**
 * @brief Test case function signature.
 */
typedef void (*test_fn_t)(void);

/**
 * @brief Named test case descriptor.
 */
typedef struct {
    const char *name;
    test_fn_t fn;
} test_case_t;

/**
 * @brief Each group exposes a getter returning a static array and its count.
 */
const test_case_t *get_tests_identity(int *out_count);

const test_case_t *get_tests_sleep_stop(int *out_count);

const test_case_t *get_tests_rr_yield(int *out_count);

const test_case_t *get_tests_rr_sleep(int *out_count);

const test_case_t *get_tests_priority(int *out_count);

const test_case_t *get_tests_coop_vs_rr(int *out_count);

const test_case_t *get_tests_tick_rate(int *out_count);

const test_case_t *get_tests_create_limits(int *out_count);

const test_case_t *get_tests_runtime_tuning(int *out_count);

const test_case_t *get_tests_fifo_order(int *out_count);

const test_case_t *get_tests_wraparound(int *out_count);

const test_case_t *get_tests_sleep_zero(int *out_count);

const test_case_t *get_tests_task_return(int *out_count);

const test_case_t *get_tests_idle_behavior(int *out_count);

const test_case_t *get_tests_semaphore(int *out_count);

const test_case_t *get_tests_queue(int *out_count);

/* External tick tests */
const test_case_t *get_tests_external_tick(int *out_count);

#ifdef HARDRT_TEST_HOOKS
/* POSIX-only test hooks to block/unblock SIGALRM for deterministic checks */
void hrt__test_block_sigalrm(void);
void hrt__test_unblock_sigalrm(void);
#endif

#endif /* HARDRT_TEST_COMMON_H */
