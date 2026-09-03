#include <stdint.h>
#include <stddef.h>

#include "hardrt.h"
#include "hardrt_sem.h"
#include "stm32h7xx.h"

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

#define HRT_TICK_BENCH_NONE          1
#define HRT_TICK_BENCH_ONE_SLEEP     2
#define HRT_TICK_BENCH_ALL_SLEEP     3
#define HRT_TICK_BENCH_ONE_EXPIRY    4
#define HRT_TICK_BENCH_SIMULTANEOUS  5
#define HRT_TICK_BENCH_STAGGERED     6

#define WORKER_COUNT (HARDRT_MAX_TASKS - 2)
#define WORKER_STACK_WORDS 256u
#define SETUP_STACK_WORDS 256u
#define LONG_SLEEP_MS 60000u

typedef struct {
    uint32_t scenario_id;
    uint32_t configured_app_tasks;
    uint32_t worker_tasks;
    uint32_t event_hz;
    uint32_t target_samples;
    uint32_t core_hz;
    uint32_t tim2_psc;
    uint32_t tim2_arr;
    uint32_t min_cycles;
    uint32_t avg_cycles;
    uint32_t max_cycles;
    uint32_t count;
    uint32_t sum_lo;
    uint32_t sum_hi;
    uint32_t worker_wakes;
    uint32_t final_tick;
    uint32_t error;
} tick_benchmark_result_t;

static volatile tick_benchmark_result_t g_result;
static volatile uint32_t g_example_error = 0u;
static volatile uint32_t g_count = 0u;
static volatile uint32_t g_min = 0xFFFFFFFFu;
static volatile uint32_t g_max = 0u;
static volatile uint64_t g_sum = 0u;
static volatile uint32_t g_worker_wakes = 0u;
static volatile uint32_t g_tim2_psc = 0u;
static volatile uint32_t g_tim2_arr = 0u;

static hrt_sem_t g_block_sem;
static uint32_t g_worker_stacks[WORKER_COUNT][WORKER_STACK_WORDS] __attribute__((aligned(8)));
static uint32_t g_setup_stack[SETUP_STACK_WORDS] __attribute__((aligned(8)));

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

static inline uint32_t cycles_now(void) {
    return DWT_CYCCNT;
}

static void dwt_init(void) {
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;
}

__attribute__((noinline, used))
void tick_benchmark_target_reached(const volatile tick_benchmark_result_t *result) {
    __asm volatile("" : : "r"(result) : "memory");
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

static void finish_result(void) {
    const uint64_t sum = g_sum;
    const uint32_t count = g_count;

    g_result.scenario_id = HRT_TICK_BENCH_SCENARIO_ID;
    g_result.configured_app_tasks = HARDRT_MAX_TASKS - 1u;
    g_result.worker_tasks = WORKER_COUNT;
    g_result.event_hz = HRT_TICK_BENCH_EVENT_HZ;
    g_result.target_samples = HRT_TICK_BENCH_TARGET_SAMPLES;
    g_result.core_hz = SystemCoreClock;
    g_result.tim2_psc = g_tim2_psc;
    g_result.tim2_arr = g_tim2_arr;
    g_result.min_cycles = g_min;
    g_result.avg_cycles = count ? (uint32_t)(sum / count) : 0u;
    g_result.max_cycles = g_max;
    g_result.count = count;
    g_result.sum_lo = (uint32_t)sum;
    g_result.sum_hi = (uint32_t)(sum >> 32u);
    g_result.worker_wakes = g_worker_wakes;
    g_result.final_tick = hrt_tick_now();
    g_result.error = g_example_error;

    tick_benchmark_target_reached(&g_result);
}

static void record_sample(uint32_t cycles) {
    if (cycles < g_min) g_min = cycles;
    if (cycles > g_max) g_max = cycles;
    g_sum += cycles;
    g_count++;
}

static void tim2_init(void) {
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    __asm volatile("dsb 0xF" ::: "memory");

    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM2RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_TIM2RST;

    const uint32_t tim_clk = SystemCoreClock / 2u;
    uint32_t psc = tim_clk / 1000000u;
    if (psc == 0u) psc = 1u;
    psc -= 1u;

    uint32_t hz = HRT_TICK_BENCH_EVENT_HZ;
    if (hz == 0u) hz = 1000u;
    uint32_t arr = 1000000u / hz;
    if (arr == 0u) arr = 1u;
    arr -= 1u;

    TIM2->PSC = psc;
    TIM2->ARR = arr;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0u;
    TIM2->DIER = TIM_DIER_UIE;
    TIM2->CR1 = TIM_CR1_ARPE;

    g_tim2_psc = psc;
    g_tim2_arr = arr;

    NVIC_SetPriority(TIM2_IRQn, 12);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void) {
    if ((TIM2->SR & TIM_SR_UIF) == 0u) return;
    TIM2->SR &= ~TIM_SR_UIF;

    if (g_count >= HRT_TICK_BENCH_TARGET_SAMPLES) return;

    const uint32_t start = cycles_now();
    hrt_tick_from_isr();
    const uint32_t end = cycles_now();
    record_sample(end - start);

    if (g_count >= HRT_TICK_BENCH_TARGET_SAMPLES) {
        TIM2->CR1 &= ~TIM_CR1_CEN;
        NVIC_DisableIRQ(TIM2_IRQn);
        finish_result();
    }
}

static void block_forever(void) {
    for (;;) {
        (void)hrt_sem_take(&g_block_sem);
    }
}

static void worker_task(void *arg) {
    const uint32_t index = (uint32_t)(uintptr_t)arg;

#if HRT_TICK_BENCH_SCENARIO_ID == HRT_TICK_BENCH_NONE
    (void)index;
    block_forever();
#elif HRT_TICK_BENCH_SCENARIO_ID == HRT_TICK_BENCH_ONE_SLEEP
    if (index != 0u) block_forever();
    for (;;) {
        hrt_sleep(LONG_SLEEP_MS);
        g_worker_wakes++;
    }
#elif HRT_TICK_BENCH_SCENARIO_ID == HRT_TICK_BENCH_ALL_SLEEP
    for (;;) {
        hrt_sleep(LONG_SLEEP_MS);
        g_worker_wakes++;
    }
#elif HRT_TICK_BENCH_SCENARIO_ID == HRT_TICK_BENCH_ONE_EXPIRY
    if (index != 0u) block_forever();
    for (;;) {
        hrt_sleep(1u);
        g_worker_wakes++;
    }
#elif HRT_TICK_BENCH_SCENARIO_ID == HRT_TICK_BENCH_SIMULTANEOUS
    for (;;) {
        hrt_sleep(1u);
        g_worker_wakes++;
    }
#elif HRT_TICK_BENCH_SCENARIO_ID == HRT_TICK_BENCH_STAGGERED
    {
        const uint32_t period_ms = index + 1u;
        for (;;) {
            hrt_sleep(period_ms);
            g_worker_wakes++;
        }
    }
#else
#error Unsupported HRT_TICK_BENCH_SCENARIO_ID
#endif
}

static void setup_task(void *arg) {
    (void)arg;
    tim2_init();
    hrt_task_delete();
}

int main(void) {
    SystemInit();
    hold_cm4();
    dwt_init();
    hrt_sem_init(&g_block_sem, 0);

    const hrt_config_t cfg = {
        .tick_hz = HRT_TICK_BENCH_EVENT_HZ,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_EXTERNAL
    };

    if (hrt_init(&cfg) != 0) example_fail(1u);

    const hrt_task_attr_t worker_attr = { .priority = HRT_PRIO0, .timeslice = 0 };
    const hrt_task_attr_t setup_attr = { .priority = HRT_PRIO1, .timeslice = 0 };

    for (uint32_t i = 0u; i < WORKER_COUNT; ++i) {
        if (hrt_create_task(worker_task, (void *)(uintptr_t)i,
                            g_worker_stacks[i], WORKER_STACK_WORDS,
                            &worker_attr) < 0) {
            example_fail(2u);
        }
    }

    if (hrt_create_task(setup_task, 0, g_setup_stack, SETUP_STACK_WORDS,
                        &setup_attr) < 0) {
        example_fail(3u);
    }

    hrt_start();
    example_fail(4u);
    return 1;
}
