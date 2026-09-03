#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"
#include "hardrt_time.h"
#include "stm32h7xx.h"

#define STACK_WORDS 512u
#define EXTERNAL_TICK_HZ 1000u
#define SLEEP_TICKS 5u
#define WAKE_SAMPLES 32u

typedef struct {
    uint32_t passed;
    uint32_t error;
    uint32_t irq_count;
    uint32_t wake_count;
    uint32_t low_counter;
    uint32_t min_delta_ticks;
    uint32_t max_delta_ticks;
    uint32_t systick_ctrl;
    uint32_t final_tick;
    uint32_t final_ms;
} hrt_external_tick_result_t;

_Static_assert(sizeof(hrt_external_tick_result_t) == 40u, "external tick result ABI");

static uint32_t stack_high[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_low[STACK_WORDS] __attribute__((aligned(8)));

static volatile hrt_external_tick_result_t g_validation_result;
volatile uint32_t g_example_error = 0u;
volatile uint32_t g_external_tick_irq_count = 0u;
volatile uint32_t g_external_tick_wake_count = 0u;
volatile uint32_t g_external_tick_low_counter = 0u;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

__attribute__((noinline, used))
void external_tick_validation_emit(const volatile hrt_external_tick_result_t *result)
{
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

static void validation_stop(uint32_t error, uint32_t passed,
                            uint32_t min_delta, uint32_t max_delta)
{
    g_example_error = error;
    g_validation_result.passed = passed;
    g_validation_result.error = error;
    g_validation_result.irq_count = g_external_tick_irq_count;
    g_validation_result.wake_count = g_external_tick_wake_count;
    g_validation_result.low_counter = g_external_tick_low_counter;
    g_validation_result.min_delta_ticks = min_delta;
    g_validation_result.max_delta_ticks = max_delta;
    g_validation_result.systick_ctrl = SysTick->CTRL;
    g_validation_result.final_tick = hrt_tick_now();
    g_validation_result.final_ms = hrt_now_ms();
    external_tick_validation_emit(&g_validation_result);
}

static void validation_fail(uint32_t error, uint32_t min_delta, uint32_t max_delta)
{
    validation_stop(error, 0u, min_delta, max_delta);
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

void TIM2_IRQHandler(void)
{
    if ((TIM2->SR & TIM_SR_UIF) == 0u) return;
    TIM2->SR &= ~TIM_SR_UIF;
    g_external_tick_irq_count++;
    hrt_tick_from_isr();
}

static void high_task(void *arg)
{
    (void)arg;
    uint32_t min_delta = UINT32_MAX;
    uint32_t max_delta = 0u;

    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0u) {
        validation_fail(401u, 0u, 0u);
    }

    for (uint32_t i = 0u; i < WAKE_SAMPLES; ++i) {
        const uint32_t before = hrt_tick_now();
        hrt_sleep(SLEEP_TICKS);
        const uint32_t after = hrt_tick_now();
        const uint32_t delta = after - before;

        if (delta < min_delta) min_delta = delta;
        if (delta > max_delta) max_delta = delta;
        if (delta != SLEEP_TICKS) validation_fail(402u, min_delta, max_delta);
        g_external_tick_wake_count++;
    }

    if (g_external_tick_wake_count != WAKE_SAMPLES) validation_fail(403u, min_delta, max_delta);
    if (g_external_tick_irq_count < WAKE_SAMPLES * SLEEP_TICKS) validation_fail(404u, min_delta, max_delta);
    if (g_external_tick_low_counter == 0u) validation_fail(405u, min_delta, max_delta);
    if (hrt_now_ms() != hrt_tick_now()) validation_fail(406u, min_delta, max_delta);

    validation_stop(0u, 1u, min_delta, max_delta);
}

static void low_task(void *arg)
{
    (void)arg;
    for (;;) {
        g_external_tick_low_counter++;
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
    if (hrt_init(&cfg) != 0) validation_stop(1u, 0u, 0u, 0u);

    tim2_start_external_tick();

    const hrt_task_attr_t high_attr = {.priority = HRT_PRIO0, .timeslice = 0u};
    const hrt_task_attr_t low_attr = {.priority = HRT_PRIO1, .timeslice = 0u};
    if (hrt_create_task(high_task, 0, stack_high, STACK_WORDS, &high_attr) < 0) validation_stop(2u, 0u, 0u, 0u);
    if (hrt_create_task(low_task, 0, stack_low, STACK_WORDS, &low_attr) < 0) validation_stop(3u, 0u, 0u, 0u);

    hrt_start();
    validation_stop(4u, 0u, 0u, 0u);
    return 1;
}
