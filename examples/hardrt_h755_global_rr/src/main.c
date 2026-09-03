#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"
#include "hardrt_sem.h"
#include "hardrt_time.h"
#include "stm32h7xx.h"

#define STACK_WORDS 512u
#define RR_SLICE_TICKS 20u
#define WAKE_DELAY_US 3000u

typedef struct {
    uint32_t passed;
    uint32_t error;
    uint32_t irq_count;
    int32_t need_switch;
    uint32_t task_a_runs;
    uint32_t task_b_runs;
    uint32_t high_runs;
    uint32_t sequence[5];
} hrt_global_rr_result_t;

_Static_assert(offsetof(hrt_global_rr_result_t, passed) == 0u, "global RR result ABI");
_Static_assert(offsetof(hrt_global_rr_result_t, need_switch) == 12u, "global RR result ABI");
_Static_assert(offsetof(hrt_global_rr_result_t, sequence) == 28u, "global RR result ABI");
_Static_assert(sizeof(hrt_global_rr_result_t) == 48u, "global RR result ABI");

static uint32_t stack_high[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_a[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_b[STACK_WORDS] __attribute__((aligned(8)));

static hrt_sem_t g_high_sem;
static volatile hrt_global_rr_result_t g_validation_result;

volatile uint32_t g_example_error = 0u;
volatile uint32_t g_irq_count = 0u;
volatile int32_t g_need_switch = -1;
volatile uint32_t g_task_a_runs = 0u;
volatile uint32_t g_task_b_runs = 0u;
volatile uint32_t g_high_runs = 0u;
volatile uint32_t g_sequence[5] = {0u, 0u, 0u, 0u, 0u};

static volatile uint32_t g_a_continued_after_irq = 0u;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

__attribute__((noinline, used))
void global_rr_validation_emit(const volatile hrt_global_rr_result_t *result)
{
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

__attribute__((noinline))
static void validation_stop(uint32_t error, uint32_t passed)
{
    g_example_error = error;
    g_validation_result.passed = passed;
    g_validation_result.error = error;
    g_validation_result.irq_count = g_irq_count;
    g_validation_result.need_switch = g_need_switch;
    g_validation_result.task_a_runs = g_task_a_runs;
    g_validation_result.task_b_runs = g_task_b_runs;
    g_validation_result.high_runs = g_high_runs;
    for (uint32_t i = 0u; i < 5u; ++i) g_validation_result.sequence[i] = g_sequence[i];
    global_rr_validation_emit(&g_validation_result);
}

static void validation_fail(uint32_t error)
{
    validation_stop(error, 0u);
}

static void validation_pass(void)
{
    validation_stop(0u, 1u);
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

    /* This is a kernel-aware IRQ below the HardRT syscall ceiling. */
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
    g_sequence[1] = 2u;

    int need_switch = -1;
    if (hrt_sem_give_from_isr(&g_high_sem, &need_switch) != 0) {
        g_need_switch = need_switch;
        return;
    }
    g_need_switch = need_switch;
}

static void high_task(void *arg)
{
    (void)arg;

    /* Created first, so global RR dispatches this task first. It deliberately
       blocks before the observed trace, leaving A then B in FIFO order. */
    if (hrt_sem_take(&g_high_sem) != 0) validation_fail(10u);

    g_high_runs++;
    g_sequence[4] = 5u;

    if (g_irq_count != 1u) validation_fail(11u);
    if (g_need_switch != 0) validation_fail(12u);
    if (g_a_continued_after_irq != 1u) validation_fail(13u);
    if (g_task_b_runs != 1u) validation_fail(14u);
    if (g_task_a_runs != 1u) validation_fail(15u);

    validation_pass();
}

static void task_a(void *arg)
{
    (void)arg;

    g_task_a_runs++;
    if (g_task_a_runs != 1u) validation_fail(20u);
    g_sequence[0] = 1u;
    tim2_start_one_shot_us(WAKE_DELAY_US);

    for (;;) {
        if (g_irq_count == 0u) continue;

        /* Global RR must not use the woken task's numeric priority to preempt
           this current READY task. Thread mode must therefore continue here. */
        if (g_high_runs != 0u) validation_fail(21u);
        if (g_need_switch != 0) validation_fail(22u);

        g_a_continued_after_irq = 1u;
        g_sequence[2] = 3u;
        hrt_yield();

        /* B and then the woken high task must run before A can return. */
        validation_fail(23u);
    }
}

static void task_b(void *arg)
{
    (void)arg;

    g_task_b_runs++;
    if (g_task_b_runs != 1u) validation_fail(30u);
    if (g_irq_count != 1u) validation_fail(31u);
    if (g_a_continued_after_irq != 1u) validation_fail(32u);
    if (g_high_runs != 0u) validation_fail(33u);

    /* B was already READY before the ISR woke high, so wake-to-tail semantics
       require B to execute before high after A yields. */
    g_sequence[3] = 4u;
    hrt_yield();
    validation_fail(34u);
}

int main(void)
{
    SystemInit();
    hold_cm4();

    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_RR,
        .default_slice = RR_SLICE_TICKS,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };

    if (hrt_init(&cfg) != 0) validation_fail(1u);
    hrt_sem_init(&g_high_sem, 0u);

    const hrt_task_attr_t high_attr = {
        .priority = HRT_PRIO0,
        .timeslice = 0u
    };
    const hrt_task_attr_t a_attr = {
        .priority = HRT_PRIO3,
        .timeslice = RR_SLICE_TICKS
    };
    const hrt_task_attr_t b_attr = {
        .priority = HRT_PRIO1,
        .timeslice = RR_SLICE_TICKS
    };

    /* Creation order is intentionally incompatible with priority order. */
    if (hrt_create_task(high_task, 0, stack_high, STACK_WORDS, &high_attr) < 0) validation_fail(2u);
    if (hrt_create_task(task_a, 0, stack_a, STACK_WORDS, &a_attr) < 0) validation_fail(3u);
    if (hrt_create_task(task_b, 0, stack_b, STACK_WORDS, &b_attr) < 0) validation_fail(4u);

    hrt_start();
    validation_fail(5u);
    return 1;
}
