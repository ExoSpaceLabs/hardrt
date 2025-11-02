/* Test runner: registers all test groups and prints a PASS/FAIL summary. */
#include "test_common.h"

/* Global failure counter used by assertion macros */
int g_failures = 0;

static void append_group(const test_case_t *group, int n, const test_case_t **out_arr, int *inout_count) {
    for (int i = 0; i < n; ++i) { out_arr[(*inout_count)++] = &group[i]; }
}

int main(void) {
    /* Collect all test groups in desired order */
    const test_case_t *registry[64];
    int total = 0;

    int n = 0;
    const test_case_t *g = NULL;

    g = get_tests_identity(&n);
    append_group(g, n, registry, &total);
    g = get_tests_sleep_stop(&n);
    append_group(g, n, registry, &total);
    g = get_tests_rr_yield(&n);
    append_group(g, n, registry, &total);
    g = get_tests_rr_sleep(&n);
    append_group(g, n, registry, &total);
    g = get_tests_priority(&n);
    append_group(g, n, registry, &total);
    g = get_tests_coop_vs_rr(&n);
    append_group(g, n, registry, &total);
    g = get_tests_tick_rate(&n);
    append_group(g, n, registry, &total);
    g = get_tests_create_limits(&n);
    append_group(g, n, registry, &total);
    g = get_tests_runtime_tuning(&n);
    append_group(g, n, registry, &total);
    g = get_tests_fifo_order(&n);
    append_group(g, n, registry, &total);
    g = get_tests_wraparound(&n);
    append_group(g, n, registry, &total);
    g = get_tests_sleep_zero(&n);
    append_group(g, n, registry, &total);
    g = get_tests_task_return(&n);
    append_group(g, n, registry, &total);
    g = get_tests_semaphore(&n);
    append_group(g, n, registry, &total);

    int tests_failed = 0;
    int tests_passed = 0;

    for (int i = 0; i < total; ++i) {
        int before = g_failures; /* snapshot assertion failures */
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
    } else {
        printf("%d assertion failure(s) in %d test(s).\n", g_failures, tests_failed);
        return 1;
    }
}
