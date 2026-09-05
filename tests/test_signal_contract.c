/* Additional explicit v0.5 signal contract coverage. */
#include "test_common.h"
#include "hardrt_event.h"

#define SIGNAL_CONTRACT_STACK_WORDS 1024
#define SIGNAL_EVENT_CYCLES 128u

static hrt_event_t g_preset_event;
static volatile int g_preset_wait_rc = -1;
static volatile hrt_event_bits_t g_preset_match = 0u;
static volatile unsigned g_repeated_cycles = 0u;
static volatile int g_repeated_error = 0;
static uint32_t g_preset_stack[SIGNAL_CONTRACT_STACK_WORDS];

static void preset_event_worker(void *arg) {
    (void)arg;

    hrt_event_bits_t matched = 0u;
    g_preset_wait_rc =
        hrt_event_wait(&g_preset_event, 0x4u, HRT_EVENT_CLEAR_ON_EXIT, &matched);
    g_preset_match = matched;
    if (g_preset_wait_rc != 0 || matched != 0x4u ||
        hrt_event_get(&g_preset_event) != 0u) {
        g_repeated_error = 1;
        hrt__test_stop_scheduler();
        hrt_yield();
        return;
    }

    for (unsigned i = 0u; i < SIGNAL_EVENT_CYCLES; ++i) {
        const hrt_event_bits_t bit =
            (hrt_event_bits_t)(1u << (i & 3u));
        if (hrt_event_set(&g_preset_event, bit) != 0 ||
            hrt_event_get(&g_preset_event) != bit) {
            g_repeated_error = 1000 + (int)i;
            break;
        }
        if (hrt_event_clear(&g_preset_event, bit) != 0 ||
            hrt_event_get(&g_preset_event) != 0u) {
            g_repeated_error = 2000 + (int)i;
            break;
        }
        g_repeated_cycles = i + 1u;
    }

    hrt__test_stop_scheduler();
    hrt_yield();
}

static void test_event_preset_and_repeated_set_clear(void) {
    hrt__test_reset_scheduler_state();
    hrt_event_init(&g_preset_event);
    g_preset_wait_rc = -1;
    g_preset_match = 0u;
    g_repeated_cycles = 0u;
    g_repeated_error = 0;

    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 0u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_SYSTICK
    };
    T_ASSERT_EQ_INT(HRT_OK, hrt_init(&cfg),
                    "event preset/repeat: init kernel");

    T_ASSERT_EQ_INT(0, hrt_event_set(&g_preset_event, 0x4u),
                    "event preset/repeat: publish bit before waiter runs");

    const hrt_task_attr_t attr = {.priority = HRT_PRIO0, .timeslice = 0u};
    T_ASSERT_TRUE(hrt_create_task(preset_event_worker, NULL, g_preset_stack,
                                  SIGNAL_CONTRACT_STACK_WORDS, &attr) >= 0,
                  "event preset/repeat: create worker");

    hrt_start();

    T_ASSERT_EQ_INT(0, g_preset_wait_rc,
                    "pre-set event makes later wait return immediately");
    T_ASSERT_EQ_UINT(0x4u, g_preset_match,
                     "pre-set wait returns the published bit");
    T_ASSERT_EQ_INT(0, g_repeated_error,
                    "repeated set/clear cycles preserve exact event state");
    T_ASSERT_EQ_UINT(SIGNAL_EVENT_CYCLES, g_repeated_cycles,
                     "all repeated event set/clear cycles complete");
}

static const test_case_t CASES[] = {
    {"Signals: event preset and repeated set/clear cycles",
     test_event_preset_and_repeated_set_clear},
};

const test_case_t *get_tests_signal_contract(int *out_count) {
    if (out_count != NULL) {
        *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    }
    return CASES;
}
