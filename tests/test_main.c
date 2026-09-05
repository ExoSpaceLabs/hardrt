/* Test runner: registers all test groups and prints a PASS/FAIL summary. */
#include "test_common.h"

const test_case_t *get_tests_preemption_contract(int *out_count);
const test_case_t *get_tests_queue_wake_policy(int *out_count);
const test_case_t *get_tests_queue_barging(int *out_count);
const test_case_t *get_tests_waitq_overflow(int *out_count);
const test_case_t *get_tests_posix_critical_mask(int *out_count);

/* Global failure counter used by assertion macros */
int g_failures = 0;

#define TEST_REGISTRY_CAPACITY 128

static int append_group(const test_case_t *group, int n,
                        const test_case_t **out_arr, int *inout_count,
                        int capacity) {
    if (group == NULL || n < 0 || inout_count == NULL || *inout_count < 0 ||
        n > capacity - *inout_count) {
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        out_arr[(*inout_count)++] = &group[i];
    }
    return 0;
}

#define APPEND_GROUP(getter) do { \
    g = getter(&n); \
    if (append_group(g, n, registry, &total, TEST_REGISTRY_CAPACITY) != 0) { \
        fprintf(stderr, "HardRT test registry capacity exceeded while adding %s\n", #getter); \
        return 2; \
    } \
} while (0)

int main(void) {
    /* Collect all test groups in desired order. Keep this bounded and checked:
     * silently overrunning the registry corrupts main()'s stack and can make a
     * later, unrelated test appear to be the failure site. */
    const test_case_t *registry[TEST_REGISTRY_CAPACITY];
    int total = 0;

    int n = 0;
    const test_case_t *g = NULL;

    APPEND_GROUP(get_tests_identity);
    APPEND_GROUP(get_tests_sleep_stop);
    APPEND_GROUP(get_tests_rr_yield);
    APPEND_GROUP(get_tests_rr_sleep);
    APPEND_GROUP(get_tests_sleep_queue);
    APPEND_GROUP(get_tests_priority);
    APPEND_GROUP(get_tests_preemption_contract);
    APPEND_GROUP(get_tests_queue_wake_policy);
    APPEND_GROUP(get_tests_queue_barging);
    APPEND_GROUP(get_tests_waitq_overflow);
    APPEND_GROUP(get_tests_posix_critical_mask);
    APPEND_GROUP(get_tests_ready_bitmap);
    APPEND_GROUP(get_tests_coop_vs_rr);
    APPEND_GROUP(get_tests_tick_rate);
    APPEND_GROUP(get_tests_create_limits);
    APPEND_GROUP(get_tests_runtime_tuning);
    APPEND_GROUP(get_tests_fifo_order);
    APPEND_GROUP(get_tests_wraparound);
    APPEND_GROUP(get_tests_sleep_zero);
    APPEND_GROUP(get_tests_task_return);
    APPEND_GROUP(get_tests_semaphore);
    APPEND_GROUP(get_tests_queue);
    APPEND_GROUP(get_tests_external_tick);
    APPEND_GROUP(get_tests_mutex);
    APPEND_GROUP(get_tests_event);
    APPEND_GROUP(get_tests_notify);
    APPEND_GROUP(get_tests_now_ms);
    APPEND_GROUP(get_tests_idle_behavior);

    int tests_failed = 0;
    int tests_passed = 0;

    for (int i = 0; i < total; ++i) {
        int before = g_failures;
        printf("\n==== Test %d/%d: %s ====%s\n", i + 1, total, registry[i]->name, "");
        registry[i]->fn();
        int after = g_failures;
        int case_failures = after - before;
        if (case_failures == 0) {
            ++tests_passed;
            printf(ANSI_GRN "RESULT" ANSI_RST ": Test %d PASSED (%s)\n", i + 1, registry[i]->name);
        } else {
            ++tests_failed;
            printf(ANSI_RED "RESULT" ANSI_RST ": Test %d FAILED (%s) — %d assertion failure(s)\n",
                   i + 1, registry[i]->name, case_failures);
        }
    }

    printf("\n================ Summary ================\n");
    printf("Total tests: %d\n", total);
    printf(ANSI_GRN "  Passed: %d" ANSI_RST "\n", tests_passed);
    if (tests_failed > 0) {
        printf(ANSI_RED "  Failed: %d" ANSI_RST "\n", tests_failed);
    } else {
        printf("  Failed: %d\n", tests_failed);
    }
    printf("========================================\n\n");

    if (g_failures == 0) {
        printf("All tests passed.\n");
        return 0;
    }

    printf("%d assertion failure(s) in %d test(s).\n", g_failures, tests_failed);
    return 1;
}
