#include <stdint.h>

#include "hardrt.h"
#include "hardrt_sem.h"
#include "hardrt_time.h"
#include "stm32h7xx.h"

#ifndef HRT_PREEMPT_CASE_ID
#define HRT_PREEMPT_CASE_ID 1
#endif

#if HRT_PREEMPT_CASE_ID != 1 && HRT_PREEMPT_CASE_ID != 2
#error "HRT_PREEMPT_CASE_ID must be 1 (priority) or 2 (priority_rr)"
#endif

#define STACK_WORDS 512u
#define RR_SLICE_TICKS 20u
#define PREEMPT_DELAY_US 3000u

static uint32_t stack_high[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_low_a[STACK_WORDS] __attribute__((aligned(8)));
#if HRT_PREEMPT_CASE_ID == 2
static uint32_t stack_low_b[STACK_WORDS] __attribute__((aligned(8)));
#endif

static hrt_sem_t g_high_sem;

volatile uint32_t g_example_error = 0u;
volatile uint32_t g_validation_pass = 0u;
volatile uint32_t g_validation_case = HRT_PREEMPT_CASE_ID;
volatile uint32_t g_irq_count = 0u;
volatile int32_t g_need_switch = -1;
volatile uint32_t g_high_runs = 0u;
volatile uint32_t g_low_a_counter = 0u;
volatile uint32_t g_low_b_counter = 0u;
volatile uint32_t g_a_start_tick = 0u;
volatile uint32_t g_irq_tick = 0u;
volatile uint32_t g_high_tick = 0u;
volatile uint32_t g_a_resume_tick = 0u;
volatile uint32_t g_b_first_tick = 0u;
volatile uint32_t g_expected_remaining_ticks = 0u;
volatile uint32_t g_observed_remaining_ticks = 0u;
volatile uint32_t g_sequence[5] = {0u, 0u, 0u, 0u, 0u};

static volatile uint32_t g_high_after_irq = 0u;
static volatile uint32_t g_a_resumed_after_high = 0u;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

__attribute__((noinline, used))
void preemption_validation_stop(uint32_t error, uint32_t passed)
{
    /* Keep all debugger-visible result state live in the linked image. */
    g_validation_case = HRT_PREEMPT_CASE_ID;
    g_example_error = error;
    g_validation_pass = passed;
    for (;;) __asm volatile("wfi");
}

static void validation_fail(uint32_t error)
{
    preemption_validation_stop(error, 0u);
}

static void validation_pass(void)
{
    preemption_validation_stop(0u, 1u);
}

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t *)(RCC_BASE_NEW + 0x0u))
#define RCC_GRSTCSETR  (*(volatile uint32_t *)(RCC_BASE_NEW + 0x8u))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

static void tim2_start_one_shot_us(uint32_t delay_us)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    __asm volatile("dsb 0xF" ::: "memory");

    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM2RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_TIM2RST;

    uint32_t tim_clk = SystemCoreClock / 2u;
    uint32_t psc = tim_clk / 1000000u;
    if (psc == 0u) psc = 1u;
    psc -= 1u;
    if (delay_us == 0u) delay_us = 1u;

    TIM2->PSC = psc;
    TIM2->ARR = delay_us - 1u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0u;
    TIM2->DIER = TIM_DIER_UIE;
    TIM2->CR1 = TIM_CR1_OPM | TIM_CR1_CEN;

    /* Numerically lower priorities are more urgent. 12 is below the HardRT
       syscall ceiling and may call the ISR-safe semaphore API. */
    NVIC_SetPriority(TIM2_IRQn, 12u);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void)
{
    if ((TIM2->SR & TIM_SR_UIF) == 0u) return;

    TIM2->SR &= ~TIM_SR_UIF;
    TIM2->DIER = 0u;
    NVIC_DisableIRQ(TIM2_IRQn);

    g_irq_count++;
    g_irq_tick = hrt_tick_now();
    g_sequence[1] = 2u;

    int need_switch = 0;
    (void)hrt_sem_give_from_isr(&g_high_sem, &need_switch);
    g_need_switch = need_switch;
}

static void high_task(void *arg)
{
    (void)arg;
    if (hrt_sem_take(&g_high_sem) != 0) validation_fail(10u);

    g_high_runs++;
    g_high_tick = hrt_tick_now();
    g_sequence[2] = 3u;
    g_high_after_irq = 1u;

    /* Leave the high-priority class so the interrupted lower-priority work can
       resume and the validator can inspect its continuation semantics. */
    hrt_sleep(1000u);
    validation_fail(11u);
}

static void low_a_task(void *arg)
{
    (void)arg;
    g_a_start_tick = hrt_tick_now();
    g_sequence[0] = 1u;
    tim2_start_one_shot_us(PREEMPT_DELAY_US);

    for (;;) {
        g_low_a_counter++;

        /* If Thread mode executes here after the ISR but before high_task, the
           ISR wake did not preempt at the earliest safe exception-return point. */
        if (g_irq_count != 0u && g_high_after_irq == 0u) {
            validation_fail(20u);
        }

        if (g_high_after_irq != 0u && g_a_resumed_after_high == 0u) {
            g_a_resumed_after_high = 1u;
            g_a_resume_tick = hrt_tick_now();
            g_sequence[3] = 4u;

            if (g_irq_count != 1u) validation_fail(21u);
            if (g_need_switch != 1) validation_fail(22u);
            if (g_high_runs != 1u) validation_fail(23u);

#if HRT_PREEMPT_CASE_ID == 1
            validation_pass();
#else
            const uint32_t consumed = g_irq_tick - g_a_start_tick;
            if (consumed == 0u || consumed >= RR_SLICE_TICKS) {
                validation_fail(24u);
            }
            g_expected_remaining_ticks = RR_SLICE_TICKS - consumed;
#endif
        }
    }
}

#if HRT_PREEMPT_CASE_ID == 2
static void low_b_task(void *arg)
{
    (void)arg;

    for (;;) {
        g_low_b_counter++;

        /* B must not get a turn before the deliberately early preemption event;
           otherwise the fixture did not interrupt A inside its first quantum. */
        if (g_irq_count == 0u) validation_fail(30u);
        if (g_high_after_irq == 0u) validation_fail(31u);

        if (g_b_first_tick == 0u) {
            g_b_first_tick = hrt_tick_now();
            g_sequence[4] = 5u;

            /* This catches the current tail-requeue defect directly: A must
               resume before its equal-priority peer B after high blocks. */
            if (g_a_resumed_after_high == 0u) validation_fail(32u);

            g_observed_remaining_ticks = g_b_first_tick - g_a_resume_tick;

            /* One tick of tolerance covers the event/tick boundary. A reset
               quantum trends toward 20 ticks; tail requeue trends toward 0. */
            const uint32_t expected = g_expected_remaining_ticks;
            const uint32_t observed = g_observed_remaining_ticks;
            if ((observed + 1u) < expected || observed > (expected + 1u)) {
                validation_fail(33u);
            }

            validation_pass();
        }
    }
}
#endif

int main(void)
{
    SystemInit();
    hold_cm4();

    const hrt_config_t cfg = {
        .tick_hz = 1000u,
#if HRT_PREEMPT_CASE_ID == 1
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0u,
#else
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = RR_SLICE_TICKS,
#endif
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };

    if (hrt_init(&cfg) != 0) validation_fail(1u);
    hrt_sem_init(&g_high_sem, 0u);

    const hrt_task_attr_t high_attr = {
        .priority = HRT_PRIO0,
        .timeslice = 0u
    };
    const hrt_task_attr_t low_attr = {
        .priority = HRT_PRIO1,
#if HRT_PREEMPT_CASE_ID == 1
        .timeslice = 0u
#else
        .timeslice = RR_SLICE_TICKS
#endif
    };

    if (hrt_create_task(high_task, 0, stack_high, STACK_WORDS, &high_attr) < 0) {
        validation_fail(2u);
    }
    if (hrt_create_task(low_a_task, 0, stack_low_a, STACK_WORDS, &low_attr) < 0) {
        validation_fail(3u);
    }
#if HRT_PREEMPT_CASE_ID == 2
    if (hrt_create_task(low_b_task, 0, stack_low_b, STACK_WORDS, &low_attr) < 0) {
        validation_fail(4u);
    }
#endif

    hrt_start();
    validation_fail(5u);
    return 1;
}
