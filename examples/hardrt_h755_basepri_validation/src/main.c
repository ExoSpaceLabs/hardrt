#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"
#include "stm32h7xx.h"

/* Private Cortex-M port hooks under qualification. They are intentionally not
 * part of the installed application API; this fixture links against the
 * reference port specifically to validate its interrupt-mask contract. */
extern void hrt_port_crit_enter(void);
extern void hrt_port_crit_exit(void);

#define STACK_WORDS 512u
#define PENDSV_EXCEPTION_NUMBER 14u
#define PENDSV_STRESS_YIELDS 20000u
#define HIGH_IRQ_PERIOD_US 101u
#define LOW_IRQ_PERIOD_US 137u

typedef struct {
    uint32_t passed;
    uint32_t error;
    uint32_t priority_step;
    uint32_t hardrt_mask;
    uint32_t zero_inside;
    uint32_t zero_after;
    uint32_t weaker_before;
    uint32_t weaker_inside;
    uint32_t weaker_nested;
    uint32_t weaker_after_inner;
    uint32_t weaker_after_outer;
    uint32_t stricter_before;
    uint32_t stricter_inside;
    uint32_t stricter_nested;
    uint32_t stricter_after_inner;
    uint32_t stricter_after_outer;
    uint32_t final_basepri;
    uint32_t pendsv_expected_basepri;
    uint32_t high_irq_count;
    uint32_t low_irq_count;
    uint32_t high_pendsv_preempt;
    uint32_t high_pendsv_mask_match;
    uint32_t high_pendsv_primask_nonzero;
    uint32_t low_pendsv_preempt;
    uint32_t low_pendsv_while_masked;
    uint32_t task_a_yields;
    uint32_t task_b_yields;
    uint32_t final_primask;
} hrt_basepri_result_t;

_Static_assert(offsetof(hrt_basepri_result_t, passed) == 0u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, hardrt_mask) == 12u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, weaker_before) == 24u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, stricter_before) == 44u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, final_basepri) == 64u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, pendsv_expected_basepri) == 68u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, high_pendsv_mask_match) == 84u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, low_pendsv_while_masked) == 96u, "BASEPRI result ABI");
_Static_assert(offsetof(hrt_basepri_result_t, final_primask) == 108u, "BASEPRI result ABI");
_Static_assert(sizeof(hrt_basepri_result_t) == 112u, "BASEPRI result ABI");

static uint32_t stack_a[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_b[STACK_WORDS] __attribute__((aligned(8)));
static volatile hrt_basepri_result_t g_result;
static volatile uint32_t g_stress_started = 0u;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

static inline void barrier(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
}

static inline uint32_t get_basepri(void)
{
    uint32_t value;
    __asm volatile("mrs %0, BASEPRI" : "=r"(value));
    return value;
}

static inline void set_basepri(uint32_t value)
{
    __asm volatile("msr BASEPRI, %0" :: "r"(value) : "memory");
    barrier();
}

static inline uint32_t get_primask(void)
{
    uint32_t value;
    __asm volatile("mrs %0, PRIMASK" : "=r"(value));
    return value;
}

__attribute__((noinline, used))
void basepri_validation_emit(const volatile hrt_basepri_result_t *result)
{
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

static void stop_irq_stress(void)
{
    TIM2->CR1 = 0u;
    TIM3->CR1 = 0u;
    TIM2->DIER = 0u;
    TIM3->DIER = 0u;
    NVIC_DisableIRQ(TIM2_IRQn);
    NVIC_DisableIRQ(TIM3_IRQn);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    barrier();
}

static void stop_validation(uint32_t error, uint32_t passed)
{
    stop_irq_stress();
    g_result.error = error;
    g_result.passed = passed;
    g_result.final_basepri = get_basepri();
    g_result.final_primask = get_primask();
    basepri_validation_emit(&g_result);
}

static void fail(uint32_t error)
{
    stop_validation(error, 0u);
}

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t *)(RCC_BASE_NEW + 0x0u))
#define RCC_GRSTCSETR  (*(volatile uint32_t *)(RCC_BASE_NEW + 0x8u))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

static void validate_direct_basepri_contract(void)
{
#ifndef __NVIC_PRIO_BITS
#error "CMSIS __NVIC_PRIO_BITS is required for BASEPRI validation"
#endif

    const uint32_t priority_step = 1u << (8u - (uint32_t)__NVIC_PRIO_BITS);
    g_result.priority_step = priority_step;

    /* Case 1: from an unmasked caller, HardRT must install its syscall ceiling
     * and then restore the exact zero state on exit. */
    set_basepri(0u);
    if (get_basepri() != 0u) fail(1u);

    hrt_port_crit_enter();
    g_result.zero_inside = get_basepri();
    g_result.hardrt_mask = g_result.zero_inside;
    if (g_result.hardrt_mask == 0u) fail(2u);
    hrt_port_crit_exit();

    g_result.zero_after = get_basepri();
    if (g_result.zero_after != 0u) fail(3u);

    const uint32_t hardrt_mask = g_result.hardrt_mask;
    if (hardrt_mask <= priority_step || hardrt_mask > (0xFFu - priority_step)) fail(4u);

    /* Case 2: a weaker pre-existing mask must be raised to HardRT's stricter
     * ceiling for the outer critical section, remain stable through nesting,
     * then restore exactly to the weaker pre-entry value. */
    const uint32_t weaker = hardrt_mask + priority_step;
    set_basepri(weaker);
    g_result.weaker_before = get_basepri();
    if (g_result.weaker_before != weaker) fail(5u);

    hrt_port_crit_enter();
    g_result.weaker_inside = get_basepri();
    if (g_result.weaker_inside != hardrt_mask) fail(6u);

    hrt_port_crit_enter();
    g_result.weaker_nested = get_basepri();
    if (g_result.weaker_nested != hardrt_mask) fail(7u);

    hrt_port_crit_exit();
    g_result.weaker_after_inner = get_basepri();
    if (g_result.weaker_after_inner != hardrt_mask) fail(8u);

    hrt_port_crit_exit();
    g_result.weaker_after_outer = get_basepri();
    if (g_result.weaker_after_outer != weaker) fail(9u);

    /* Case 3: a stricter pre-existing mask must never be weakened. */
    const uint32_t stricter = hardrt_mask - priority_step;
    set_basepri(stricter);
    g_result.stricter_before = get_basepri();
    if (g_result.stricter_before != stricter) fail(10u);

    hrt_port_crit_enter();
    g_result.stricter_inside = get_basepri();
    if (g_result.stricter_inside != stricter) fail(11u);

    hrt_port_crit_enter();
    g_result.stricter_nested = get_basepri();
    if (g_result.stricter_nested != stricter) fail(12u);

    hrt_port_crit_exit();
    g_result.stricter_after_inner = get_basepri();
    if (g_result.stricter_after_inner != stricter) fail(13u);

    hrt_port_crit_exit();
    g_result.stricter_after_outer = get_basepri();
    if (g_result.stricter_after_outer != stricter) fail(14u);

    set_basepri(0u);
    g_result.pendsv_expected_basepri = hardrt_mask;
}

static uint32_t preempted_ipsr(const uint32_t *frame)
{
    return frame[7] & 0x1FFu;
}

__attribute__((used, noinline))
void tim2_irq_body(uint32_t *frame)
{
    if ((TIM2->SR & TIM_SR_UIF) == 0u) return;
    TIM2->SR &= ~TIM_SR_UIF;
    g_result.high_irq_count++;

    if (preempted_ipsr(frame) == PENDSV_EXCEPTION_NUMBER) {
        const uint32_t basepri = get_basepri();
        const uint32_t primask = get_primask();
        g_result.high_pendsv_preempt++;
        if (basepri == g_result.pendsv_expected_basepri)
            g_result.high_pendsv_mask_match++;
        if (primask != 0u)
            g_result.high_pendsv_primask_nonzero++;
    }
}

__attribute__((used, noinline))
void tim3_irq_body(uint32_t *frame)
{
    if ((TIM3->SR & TIM_SR_UIF) == 0u) return;
    TIM3->SR &= ~TIM_SR_UIF;
    g_result.low_irq_count++;

    if (preempted_ipsr(frame) == PENDSV_EXCEPTION_NUMBER) {
        g_result.low_pendsv_preempt++;
        if (get_basepri() != 0u)
            g_result.low_pendsv_while_masked++;
    }
}

__attribute__((naked))
void TIM2_IRQHandler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b tim2_irq_body\n");
}

__attribute__((naked))
void TIM3_IRQHandler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b tim3_irq_body\n");
}

static void configure_irq_stress(void)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN | RCC_APB1LENR_TIM3EN;
    barrier();
    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM2RST | RCC_APB1LRSTR_TIM3RST;
    RCC->APB1LRSTR &= ~(RCC_APB1LRSTR_TIM2RST | RCC_APB1LRSTR_TIM3RST);

    uint32_t tim_clk = SystemCoreClock / 2u;
    uint32_t psc = tim_clk / 1000000u;
    if (psc == 0u) psc = 1u;
    psc -= 1u;

    TIM2->PSC = psc;
    TIM2->ARR = HIGH_IRQ_PERIOD_US - 1u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0u;
    TIM2->DIER = TIM_DIER_UIE;

    TIM3->PSC = psc;
    TIM3->ARR = LOW_IRQ_PERIOD_US - 1u;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0u;
    TIM3->DIER = TIM_DIER_UIE;

    const uint32_t shift = 8u - (uint32_t)__NVIC_PRIO_BITS;
    const uint32_t ceiling = g_result.pendsv_expected_basepri >> shift;
    const uint32_t max_prio = (1u << (uint32_t)__NVIC_PRIO_BITS) - 1u;
    if (ceiling == 0u || ceiling >= max_prio) fail(20u);

    NVIC_SetPriority(TIM2_IRQn, ceiling - 1u);
    NVIC_SetPriority(TIM3_IRQn, ceiling + 1u);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);
}

static void start_irq_stress(void)
{
    TIM2->CNT = 0u;
    TIM3->CNT = 0u;
    g_stress_started = 1u;
    TIM2->CR1 = TIM_CR1_CEN;
    TIM3->CR1 = TIM_CR1_CEN;
    barrier();
}

static void task_a(void *arg)
{
    (void)arg;
    start_irq_stress();

    for (uint32_t i = 0u; i < PENDSV_STRESS_YIELDS; ++i) {
        g_result.task_a_yields++;
        hrt_yield();
    }

    stop_irq_stress();

    if (g_result.high_irq_count == 0u) fail(101u);
    if (g_result.low_irq_count == 0u) fail(102u);
    if (g_result.high_pendsv_preempt == 0u) fail(103u);
    if (g_result.high_pendsv_mask_match == 0u) fail(104u);
    if (g_result.high_pendsv_primask_nonzero != 0u) fail(105u);
    if (g_result.low_pendsv_while_masked != 0u) fail(106u);
    if (g_result.task_b_yields == 0u) fail(107u);
    if (get_basepri() != 0u) fail(108u);
    if (get_primask() != 0u) fail(109u);

    stop_validation(0u, 1u);
}

static void task_b(void *arg)
{
    (void)arg;
    while (g_stress_started == 0u) hrt_yield();
    for (;;) {
        g_result.task_b_yields++;
        hrt_yield();
    }
}

int main(void)
{
    SystemInit();
    hold_cm4();

    validate_direct_basepri_contract();

    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_RR,
        .default_slice = 0u,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_EXTERNAL
    };
    if (hrt_init(&cfg) != 0) fail(30u);

    configure_irq_stress();

    const hrt_task_attr_t attr = {.priority = HRT_PRIO1, .timeslice = 0u};
    if (hrt_create_task(task_a, 0, stack_a, STACK_WORDS, &attr) < 0) fail(31u);
    if (hrt_create_task(task_b, 0, stack_b, STACK_WORDS, &attr) < 0) fail(32u);

    hrt_start();
    fail(33u);
    return 1;
}
