/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

#include "hardrt.h"
#include "stm32h7xx.h"

#define STACK_WORDS 512u
#define FP_REG_COUNT 32u
#define PASS_ITERATIONS 1000u

static uint32_t stack_a[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_b[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t g_bank_a[FP_REG_COUNT] __attribute__((aligned(8)));
static uint32_t g_bank_b[FP_REG_COUNT] __attribute__((aligned(8)));
static uint32_t g_observed_a[FP_REG_COUNT] __attribute__((aligned(8)));
static uint32_t g_observed_b[FP_REG_COUNT] __attribute__((aligned(8)));

volatile uint32_t g_fp_validation_pass = 0u;
volatile uint32_t g_fp_validation_error = 0u;
volatile uint32_t g_fp_a_iterations = 0u;
volatile uint32_t g_fp_b_iterations = 0u;
volatile uint32_t g_fp_last_bad_reg = 0xFFFFFFFFu;
volatile uint32_t g_fp_expected = 0u;
volatile uint32_t g_fp_actual = 0u;
volatile uint32_t g_fp_expected_fpscr = 0u;
volatile uint32_t g_fp_actual_fpscr = 0u;
volatile uint32_t g_example_error = 0u;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

extern void hrt_fp_roundtrip_yield(const uint32_t *bank_in,
                                   uint32_t fpscr_in,
                                   uint32_t *bank_out,
                                   uint32_t *fpscr_out);

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t *)(RCC_BASE_NEW + 0x0u))
#define RCC_GRSTCSETR  (*(volatile uint32_t *)(RCC_BASE_NEW + 0x8u))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

static void validation_fail(uint32_t code,
                            uint32_t reg,
                            uint32_t expected,
                            uint32_t actual)
{
    g_fp_validation_error = code;
    g_example_error = code;
    g_fp_last_bad_reg = reg;
    g_fp_expected = expected;
    g_fp_actual = actual;
    for (;;) __asm volatile("wfi");
}

static void compare_state(const uint32_t *expected,
                          const uint32_t *actual,
                          uint32_t expected_fpscr,
                          uint32_t actual_fpscr,
                          uint32_t error_base)
{
    for (uint32_t i = 0u; i < FP_REG_COUNT; ++i) {
        if (actual[i] != expected[i]) {
            validation_fail(error_base + 1u, i, expected[i], actual[i]);
        }
    }
    if (actual_fpscr != expected_fpscr) {
        g_fp_expected_fpscr = expected_fpscr;
        g_fp_actual_fpscr = actual_fpscr;
        validation_fail(error_base + 2u, FP_REG_COUNT,
                        expected_fpscr, actual_fpscr);
    }
}

static void maybe_mark_pass(void)
{
    if (g_fp_a_iterations >= PASS_ITERATIONS &&
        g_fp_b_iterations >= PASS_ITERATIONS) {
        g_fp_validation_pass = 1u;
    }
}

static void task_a(void *arg)
{
    (void)arg;
    const uint32_t fpscr = 0x00000000u;
    for (;;) {
        uint32_t observed_fpscr = 0u;
        hrt_fp_roundtrip_yield(g_bank_a, fpscr, g_observed_a, &observed_fpscr);
        compare_state(g_bank_a, g_observed_a, fpscr, observed_fpscr, 100u);
        g_fp_a_iterations++;
        maybe_mark_pass();
    }
}

static void task_b(void *arg)
{
    (void)arg;
    /* FPSCR RMode=11 (round toward zero). No exception enables/flags are set. */
    const uint32_t fpscr = 0x00C00000u;
    for (;;) {
        uint32_t observed_fpscr = 0u;
        hrt_fp_roundtrip_yield(g_bank_b, fpscr, g_observed_b, &observed_fpscr);
        compare_state(g_bank_b, g_observed_b, fpscr, observed_fpscr, 200u);
        g_fp_b_iterations++;
        maybe_mark_pass();
    }
}

int main(void)
{
    SystemInit();
    hold_cm4();

    /* Distinct, finite single-precision bit patterns. The test performs no FP
       arithmetic on them; exact bit identity across a switch is required. */
    for (uint32_t i = 0u; i < FP_REG_COUNT; ++i) {
        g_bank_a[i] = 0x3F000000u + (i << 16);
        g_bank_b[i] = 0xBF000000u + (i << 16);
        g_observed_a[i] = 0u;
        g_observed_b[i] = 0u;
    }

    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_RR,
        .default_slice = 1u,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };
    if (hrt_init(&cfg) != 0) validation_fail(1u, 0u, 0u, 0u);

    const hrt_task_attr_t attr = {
        .priority = HRT_PRIO0,
        .timeslice = 1u
    };
    if (hrt_create_task(task_a, 0, stack_a, STACK_WORDS, &attr) < 0) {
        validation_fail(2u, 0u, 0u, 0u);
    }
    if (hrt_create_task(task_b, 0, stack_b, STACK_WORDS, &attr) < 0) {
        validation_fail(3u, 0u, 0u, 0u);
    }

    hrt_start();
    validation_fail(4u, 0u, 0u, 0u);
    return 1;
}
