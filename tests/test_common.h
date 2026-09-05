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
void hrt__test_idle_counter_reset(void);
unsigned long long hrt__test_idle_counter_value(void);
void hrt__test_fast_forward_ticks(uint32_t delta);
void hrt__test_set_tick(uint32_t v);
uint32_t hrt__test_get_tick(void);
int hrt__test_task_state(int id);
int hrt__test_slot_state(int id);
#endif
#ifdef __cplusplus
}
#endif

#define ANSI_GRN "\x1b[32m"
#define ANSI_RED "\x1b[31m"
#define ANSI_RST "\x1b[0m"

extern int g_failures;

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

void hrt__inc_tick(void);

#define T_ASSERT_EQ_UINT(exp, got, msg) do { \
    if ((exp) != (got)) { \
        ++g_failures; \
        printf(ANSI_RED "FAIL" ANSI_RST ": %s\n", msg); \
        printf("  expected %u, got %u (%s:%d)\n", (unsigned)(exp), (unsigned)(got), __FILE__, __LINE__); \
    } else { \
        printf(ANSI_GRN "PASS" ANSI_RST ": %s (=%u)\n", msg, (unsigned)(got)); \
    } \
} while(0)

typedef void (*test_fn_t)(void);

typedef struct {
    const char *name;
    test_fn_t fn;
} test_case_t;

const test_case_t *get_tests_identity(int *out_count);
const test_case_t *get_tests_sleep_stop(int *out_count);
const test_case_t *get_tests_lifecycle(int *out_count);
const test_case_t *get_tests_rr_yield(int *out_count);
const test_case_t *get_tests_rr_sleep(int *out_count);
const test_case_t *get_tests_sleep_queue(int *out_count);
const test_case_t *get_tests_priority(int *out_count);
const test_case_t *get_tests_preemption_contract(int *out_count);
const test_case_t *get_tests_ready_bitmap(int *out_count);
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
const test_case_t *get_tests_mutex(int *out_count);
const test_case_t *get_tests_now_ms(int *out_count);
const test_case_t *get_tests_external_tick(int *out_count);

#ifdef HARDRT_TEST_HOOKS
void hrt__test_block_sigalrm(void);
void hrt__test_unblock_sigalrm(void);
#endif

#endif /* HARDRT_TEST_COMMON_H */
