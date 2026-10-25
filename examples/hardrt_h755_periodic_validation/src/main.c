#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"
#include "hardrt_time.h"
#include "stm32h7xx.h"

#define STACK_WORDS 512u
#define EXTERNAL_TICK_HZ 1000u
#define PERIOD_TICKS 5u
#define WORK_TICKS 2u
#define RELEASE_SAMPLES 16u
#define INTENTIONAL_LATENESS 3u

typedef struct {
    uint32_t passed;
    uint32_t error;
    uint32_t irq_count;
    uint32_t release_count;
    uint32_t low_counter;
    uint32_t phase_tick;
    uint32_t expected_last_release;
    uint32_t observed_last_release;
    uint32_t miss_result;
    uint32_t miss_lateness;
    uint32_t exact_result;
    uint32_t exact_lateness;
    uint32_t final_tick;
} hrt_periodic_validation_result_t;

_Static_assert(sizeof(hrt_periodic_validation_result_t) == 52u,
               "periodic validation result ABI");

static uint32_t stack_periodic[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_low[STACK_WORDS] __attribute__((aligned(8)));

static volatile hrt_periodic_validation_result_t g_validation_result;
volatile uint32_t g_periodic_error = 0u;
volatile uint32_t g_periodic_irq_count = 0u;
volatile uint32_t g_periodic_release_count = 0u;
volatile uint32_t g_periodic_low_counter = 0u;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

__attribute__((noinline, used))
void periodic_validation_emit(
    const volatile hrt_periodic_validation_result_t *result)
{
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

static void validation_stop(uint32_t error,
                            uint32_t phase_tick,
                            uint32_t expected_last_release,
                            uint32_t observed_last_release,
                            hrt_delay_result_t miss_result,
                            hrt_tick_t miss_lateness,
                            hrt_delay_result_t exact_result,
                            hrt_tick_t exact_lateness)
{
    g_periodic_error = error;
    g_validation_result.passed = (error == 0u) ? 1u : 0u;
    g_validation_result.error = error;
    g_validation_result.irq_count = g_periodic_irq_count;
    g_validation_result.release_count = g_periodic_release_count;
    g_validation_result.low_counter = g_periodic_low_counter;
    g_validation_result.phase_tick = phase_tick;
    g_validation_result.expected_last_release = expected_last_release;
    g_validation_result.observed_last_release = observed_last_release;
    g_validation_result.miss_result = (uint32_t)miss_result;
    g_validation_result.miss_lateness = miss_lateness;
    g_validation_result.exact_result = (uint32_t)exact_result;
    g_validation_result.exact_lateness = exact_lateness;
    g_validation_result.final_tick = hrt_tick_now();
    periodic_validation_emit(&g_validation_result);
}

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t *)(RCC_BASE_NEW + 0x0u))
#define RCC_GRSTCSETR  (*(volatile uint32_t *)(RCC_BASE_NEW + 0x8u))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

static void tim2_start_external_tick(void)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    __asm volatile("dsb 0xF" ::: "memory");
    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM2RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_TIM2RST;

    uint32_t tim_clk = SystemCoreClock / 2u;
    uint32_t psc = tim_clk / 1000000u;
    if (psc == 0u) psc = 1u;
    psc -= 1u;

    TIM2->PSC = psc;
    TIM2->ARR = (1000000u / EXTERNAL_TICK_HZ) - 1u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0u;
    TIM2->DIER = TIM_DIER_UIE;

    NVIC_SetPriority(TIM2_IRQn, 12u);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
}

static void tim2_freeze_external_tick(void)
{
    TIM2->DIER &= ~TIM_DIER_UIE;
    NVIC_DisableIRQ(TIM2_IRQn);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    TIM2->SR = 0u;
}

void TIM2_IRQHandler(void)
{
    if ((TIM2->SR & TIM_SR_UIF) == 0u) return;
    TIM2->SR &= ~TIM_SR_UIF;
    g_periodic_irq_count++;
    hrt_tick_from_isr();
}

static void periodic_task(void *arg)
{
    (void)arg;

    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0u) {
        validation_stop(501u, 0u, 0u, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }

    tim2_start_external_tick();

    const hrt_tick_t phase = hrt_tick_now();
    hrt_tick_t next_release = phase;
    hrt_tick_t observed_last = phase;

    for (uint32_t i = 0u; i < RELEASE_SAMPLES; ++i) {
        next_release += PERIOD_TICKS;

        /*
         * Consume real CPU time for WORK_TICKS before waiting. TIM2 continues
         * to advance kernel time while this highest-priority task runs. The
         * remaining wait must therefore shrink instead of shifting phase.
         */
        const hrt_tick_t work_start = hrt_tick_now();
        while ((hrt_tick_t)(hrt_tick_now() - work_start) < WORK_TICKS) {
            __asm volatile("nop");
        }

        hrt_tick_t lateness = UINT32_MAX;
        const hrt_delay_result_t result =
            hrt_delay_until(next_release, &lateness);
        const hrt_tick_t observed = hrt_tick_now();

        if (result != HRT_DELAY_OK) {
            validation_stop(502u, phase, next_release, observed,
                            result, lateness,
                            HRT_DELAY_INVALID_CONTEXT, 0u);
        }
        if (lateness != 0u) {
            validation_stop(503u, phase, next_release, observed,
                            result, lateness,
                            HRT_DELAY_INVALID_CONTEXT, 0u);
        }
        if (observed != next_release) {
            validation_stop(504u, phase, next_release, observed,
                            result, lateness,
                            HRT_DELAY_INVALID_CONTEXT, 0u);
        }

        observed_last = observed;
        g_periodic_release_count++;
    }

    if (g_periodic_release_count != RELEASE_SAMPLES) {
        validation_stop(505u, phase, next_release, observed_last,
                        HRT_DELAY_INVALID_CONTEXT, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }
    if (g_periodic_low_counter == 0u) {
        validation_stop(506u, phase, next_release, observed_last,
                        HRT_DELAY_INVALID_CONTEXT, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }

    /*
     * Freeze the application-owned tick to make the explicit miss/exact tests
     * deterministic. These calls do not sleep and therefore do not need time
     * to advance.
     */
    tim2_freeze_external_tick();

    const hrt_tick_t frozen_now = hrt_tick_now();
    hrt_tick_t miss_lateness = UINT32_MAX;
    const hrt_delay_result_t miss_result =
        hrt_delay_until(frozen_now - INTENTIONAL_LATENESS, &miss_lateness);

    if (miss_result != HRT_DELAY_MISSED) {
        validation_stop(507u, phase, next_release, observed_last,
                        miss_result, miss_lateness,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }
    if (miss_lateness != INTENTIONAL_LATENESS) {
        validation_stop(508u, phase, next_release, observed_last,
                        miss_result, miss_lateness,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }
    if (hrt_tick_now() != frozen_now) {
        validation_stop(509u, phase, next_release, observed_last,
                        miss_result, miss_lateness,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }

    hrt_tick_t exact_lateness = UINT32_MAX;
    const hrt_delay_result_t exact_result =
        hrt_delay_until(frozen_now, &exact_lateness);

    if (exact_result != HRT_DELAY_OK) {
        validation_stop(510u, phase, next_release, observed_last,
                        miss_result, miss_lateness,
                        exact_result, exact_lateness);
    }
    if (exact_lateness != 0u) {
        validation_stop(511u, phase, next_release, observed_last,
                        miss_result, miss_lateness,
                        exact_result, exact_lateness);
    }

    validation_stop(0u, phase, next_release, observed_last,
                    miss_result, miss_lateness,
                    exact_result, exact_lateness);
}

static void low_task(void *arg)
{
    (void)arg;
    for (;;) {
        g_periodic_low_counter++;
        __asm volatile("nop");
    }
}

int main(void)
{
    SystemInit();
    hold_cm4();

    const hrt_config_t cfg = {
        .tick_hz = EXTERNAL_TICK_HZ,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0u,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_EXTERNAL
    };
    if (hrt_init(&cfg) != HRT_OK) {
        validation_stop(1u, 0u, 0u, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }

    const hrt_task_attr_t periodic_attr = {
        .priority = HRT_PRIO0,
        .timeslice = 0u
    };
    const hrt_task_attr_t low_attr = {
        .priority = HRT_PRIO1,
        .timeslice = 0u
    };

    if (hrt_create_task(periodic_task, NULL, stack_periodic, STACK_WORDS,
                        &periodic_attr) < 0) {
        validation_stop(2u, 0u, 0u, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }
    if (hrt_create_task(low_task, NULL, stack_low, STACK_WORDS,
                        &low_attr) < 0) {
        validation_stop(3u, 0u, 0u, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u,
                        HRT_DELAY_INVALID_CONTEXT, 0u);
    }

    hrt_start();
    validation_stop(4u, 0u, 0u, 0u,
                    HRT_DELAY_INVALID_CONTEXT, 0u,
                    HRT_DELAY_INVALID_CONTEXT, 0u);
    return 1;
}
