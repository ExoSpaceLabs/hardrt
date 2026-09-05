#include "test_common.h"

#define STACK_WORDS 1024

static uint32_t g_stack_a[STACK_WORDS];
static uint32_t g_stack_b[STACK_WORDS];

extern volatile hrt_err g_error;

static void inert_task(void *arg) {
    (void)arg;
    for (;;) hrt_yield();
}

static void stop_scheduler_task(void *arg) {
    (void)arg;
    hrt__test_stop_scheduler();
    hrt_yield();
    for (;;) hrt_yield();
}

static hrt_config_t external_cfg(void) {
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000u;
    cfg.policy = HRT_SCHED_PRIORITY_RR;
    cfg.default_slice = 3u;
    cfg.core_hz = 0u;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    return cfg;
}

static void test_null_defaults_and_double_init(void) {
    hrt__test_reset_scheduler_state();
    g_error = NONE;

    T_ASSERT_EQ_INT(HRT_OK, hrt_init(NULL),
                    "NULL config initializes documented defaults");
    T_ASSERT_EQ_INT(HRT_ERR_ALREADY_INITIALIZED, hrt_init(NULL),
                    "second init is rejected predictably");
    T_ASSERT_EQ_INT(ERR_INVALID_STATE, g_error,
                    "double init records lifecycle diagnostic");

    hrt__test_reset_scheduler_state();
}

static void test_valid_explicit_configuration(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = external_cfg();
    cfg.policy = HRT_SCHED_PRIORITY;
    cfg.default_slice = 0u;

    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg),
                    "valid explicit configuration initializes successfully");
    hrt__test_reset_scheduler_state();

    cfg = external_cfg();
    cfg.tick_hz = HRT_TICK_HZ_MAX;
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg),
                    "external tick accepts the full documented non-zero uint32 range");
    hrt__test_reset_scheduler_state();

    cfg = external_cfg();
    cfg.core_hz = 64000000u;
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg),
                    "unused core_hz is accepted for external tick compatibility");
    hrt__test_reset_scheduler_state();
}

static void test_invalid_configuration_values(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = external_cfg();

    cfg.tick_hz = 0u;
    T_ASSERT_EQ_INT(HRT_ERR_INVALID_CONFIG, hrt_init(&cfg),
                    "explicit zero tick frequency is rejected");

    cfg = external_cfg();
    cfg.policy = (hrt_policy_t)99;
    T_ASSERT_EQ_INT(HRT_ERR_INVALID_CONFIG, hrt_init(&cfg),
                    "invalid scheduler policy is rejected");

    cfg = external_cfg();
    cfg.tick_src = (hrt_tick_source_t)99;
    T_ASSERT_EQ_INT(HRT_ERR_INVALID_CONFIG, hrt_init(&cfg),
                    "invalid tick source is rejected");

    hrt__test_reset_scheduler_state();
}

static void test_port_init_failure_can_retry(void) {
    hrt__test_reset_scheduler_state();

    hrt_config_t cfg = external_cfg();
    cfg.tick_src = HRT_TICK_SYSTICK;
    cfg.tick_hz = 2000000u; /* POSIX timer period would truncate to zero us. */
    T_ASSERT_EQ_INT(HRT_ERR_PORT_INIT, hrt_init(&cfg),
                    "unrepresentable port tick reports port-init failure");

    cfg = external_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg),
                    "failed init leaves lifecycle retryable");

    hrt__test_reset_scheduler_state();
}

static void test_create_and_start_before_init_fail(void) {
    hrt__test_reset_scheduler_state();
    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };

    T_ASSERT_EQ_INT(-1, hrt_create_task(inert_task, NULL,
                                       g_stack_a, STACK_WORDS, &attr),
                    "task creation before init is rejected");
    T_ASSERT_EQ_INT(ERR_INVALID_STATE, g_error,
                    "pre-init task creation records lifecycle diagnostic");
    T_ASSERT_EQ_INT(HRT_ERR_INVALID_STATE, hrt_start(),
                    "scheduler start before init is rejected");

    hrt__test_reset_scheduler_state();
}

static void test_running_state_allows_creation_but_rejects_restart(void) {
    hrt__test_reset_scheduler_state();
    hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg),
                    "init running-state lifecycle fixture");

    const hrt_task_attr_t attr = { .priority = HRT_PRIO0, .timeslice = 0u };
    T_ASSERT_TRUE(hrt_create_task(stop_scheduler_task, NULL,
                                  g_stack_a, STACK_WORDS, &attr) >= 0,
                  "created task that returns control to POSIX test scheduler");

    T_ASSERT_EQ_INT(HRT_OK, hrt_start(),
                    "successful scheduler entry reports HRT_OK when test port returns");
    T_ASSERT_TRUE(hrt_create_task(inert_task, NULL,
                                  g_stack_b, STACK_WORDS, &attr) >= 0,
                  "task creation remains supported after scheduler start");
    T_ASSERT_EQ_INT(HRT_ERR_INVALID_STATE, hrt_start(),
                    "second scheduler start is rejected");

    hrt__test_reset_scheduler_state();
}

static const test_case_t CASES[] = {
    {"Lifecycle: NULL defaults and double init", test_null_defaults_and_double_init},
    {"Lifecycle: valid explicit configuration", test_valid_explicit_configuration},
    {"Lifecycle: invalid configuration values", test_invalid_configuration_values},
    {"Lifecycle: port init failure remains retryable", test_port_init_failure_can_retry},
    {"Lifecycle: create/start before init fail", test_create_and_start_before_init_fail},
    {"Lifecycle: RUNNING allows creation but rejects restart", test_running_state_allows_creation_but_rejects_restart},
};

const test_case_t *get_tests_lifecycle(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}
