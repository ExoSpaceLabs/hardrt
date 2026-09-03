#include <stdint.h>

#include "hardrt.h"
#include "hardrt_sem.h"
#include "stm32h7xx.h"
#include "timing_shared.h"

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

volatile hrt_timing_stats_t g_timing_stats;
volatile uint32_t g_timing_start_cycles = 0;
volatile uint32_t g_timing_case_id = HRT_TIMING_CASE_ID;
volatile uint32_t g_timing_event_hz = HRT_TIMING_EVENT_HZ;
volatile uint32_t g_timing_target_samples = HRT_TIMING_TARGET_SAMPLES;
volatile uint32_t g_example_error = 0;

volatile uint32_t tim2_psc_dbg __attribute__((used)) = 0;
volatile uint32_t tim2_arr_dbg __attribute__((used)) = 0;

static hrt_sem_t g_event_sem;
static volatile uint32_t g_event_armed = 0;

#define STACK_WORDS 1024
static uint32_t g_latency_stack[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t g_starter_stack[STACK_WORDS] __attribute__((aligned(8)));

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

static inline uint32_t cycles_now(void) {
    return DWT_CYCCNT;
}

static void dwt_init(void) {
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
}

static void stats_init(volatile hrt_timing_stats_t *s) {
    s->min = 0xFFFFFFFFu;
    s->max = 0u;
    s->avg = 0u;
    s->count = 0u;
    s->sum = 0u;
}

__attribute__((noinline, used))
void timing_target_reached(void) {
    /* GDB owns the breakpoint at function entry. Keeping the result stop free
       of a firmware BKPT avoids the trap racing the scripted breakpoint. */
    for (;;) __asm volatile("wfi");
}

__attribute__((noinline, used))
static void example_fail(uint32_t code) {
    g_example_error = code;
    __asm volatile("bkpt 0");
    for (;;) __asm volatile("wfi");
}

static inline void hold_cm4(void) {
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t *)(RCC_BASE_NEW + 0x0))
#define RCC_GRSTCSETR  (*(volatile uint32_t *)(RCC_BASE_NEW + 0x8))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

static void tim2_init_event(uint32_t hz) {
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    __asm volatile("dsb 0xF" ::: "memory");

    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM2RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_TIM2RST;

    const uint32_t tim_clk = SystemCoreClock / 2u;
    uint32_t psc = tim_clk / 1000000u;
    if (psc == 0u) psc = 1u;
    psc -= 1u;

    if (hz == 0u) hz = 1000u;
    uint32_t arr = 1000000u / hz;
    if (arr == 0u) arr = 1u;
    arr -= 1u;

    TIM2->PSC = psc;
    TIM2->ARR = arr;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UIE;
    TIM2->CR1 = 0;

    tim2_psc_dbg = psc;
    tim2_arr_dbg = arr;

    NVIC_SetPriority(TIM2_IRQn, 12);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_DisableIRQ(TIM2_IRQn);
}

static void tim2_arm_one_shot(void) {
    TIM2->CR1 &= ~TIM_CR1_CEN;
    TIM2->CNT = 0;
    TIM2->SR = 0;
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void) {
    if ((TIM2->SR & TIM_SR_UIF) == 0u) return;

    TIM2->SR &= ~TIM_SR_UIF;
    TIM2->CR1 &= ~TIM_CR1_CEN;
    NVIC_DisableIRQ(TIM2_IRQn);

    if (g_timing_stats.count >= g_timing_target_samples) return;

#if HRT_TIMING_CASE_ID == HRT_TIMING_CASE_EVENT_TO_TASK
    g_timing_start_cycles = cycles_now();
    g_event_armed = 1u;
#endif

    int should_switch = 0;
    (void)hrt_sem_give_from_isr(&g_event_sem, &should_switch);
    (void)should_switch;
}

static void latency_task(void *arg) {
    (void)arg;

    for (;;) {
        (void)hrt_sem_take(&g_event_sem);

#if HRT_TIMING_CASE_ID == HRT_TIMING_CASE_EVENT_TO_TASK
        if (g_event_armed != 0u) {
            const uint32_t end = cycles_now();
            g_event_armed = 0u;
            hrt_timing_stats_record(&g_timing_stats, end - g_timing_start_cycles);
        }
#endif

        if (g_timing_stats.count >= g_timing_target_samples) {
            timing_target_reached();
        }

        /* One-shot re-arm leaves a full timer period for this task to block. */
        tim2_arm_one_shot();
    }
}

static void timer_starter_task(void *arg) {
    (void)arg;
    tim2_init_event(g_timing_event_hz);
    tim2_arm_one_shot();
    hrt_task_delete();
}

int main(void) {
    SystemInit();
    hold_cm4();
    dwt_init();
    stats_init(&g_timing_stats);
    hrt_sem_init(&g_event_sem, 0);

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };

    if (hrt_init(&cfg) != 0) example_fail(1u);

    const hrt_task_attr_t latency_attr = { .priority = HRT_PRIO0, .timeslice = 0 };
    const hrt_task_attr_t starter_attr = { .priority = HRT_PRIO1, .timeslice = 0 };

    if (hrt_create_task(latency_task, 0, g_latency_stack,
                        sizeof(g_latency_stack) / sizeof(g_latency_stack[0]),
                        &latency_attr) < 0) {
        example_fail(2u);
    }
    if (hrt_create_task(timer_starter_task, 0, g_starter_stack,
                        sizeof(g_starter_stack) / sizeof(g_starter_stack[0]),
                        &starter_attr) < 0) {
        example_fail(3u);
    }

    hrt_start();
    example_fail(4u);
    return 1;
}
