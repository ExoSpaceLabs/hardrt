#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"
#include "hardrt_sem.h"
#include "stm32h7xx.h"
#include "timing_shared.h"

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

typedef struct {
    uint32_t case_id;
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
    uint32_t error;

    /* Scheduler/PendSV breakdown. The first 52 bytes above remain compatible
       with the existing timing result ABI for all other timing cases. */
    uint32_t pendsv_save_min;
    uint32_t pendsv_save_avg;
    uint32_t pendsv_save_max;
    uint32_t pendsv_restore_min;
    uint32_t pendsv_restore_avg;
    uint32_t pendsv_restore_max;
    uint32_t pendsv_software_min;
    uint32_t pendsv_software_avg;
    uint32_t pendsv_software_max;
    uint32_t pendsv_to_task_min;
    uint32_t pendsv_to_task_avg;
    uint32_t pendsv_to_task_max;
} hrt_timing_result_t;

_Static_assert(offsetof(hrt_timing_result_t, case_id) == 0u, "timing result ABI");
_Static_assert(offsetof(hrt_timing_result_t, count) == 36u, "timing result ABI");
_Static_assert(offsetof(hrt_timing_result_t, sum_lo) == 40u, "timing result ABI");
_Static_assert(offsetof(hrt_timing_result_t, error) == 48u, "timing result ABI");
_Static_assert(offsetof(hrt_timing_result_t, pendsv_save_min) == 52u, "timing breakdown ABI");
_Static_assert(sizeof(hrt_timing_result_t) == 100u, "timing breakdown ABI");

volatile hrt_timing_stats_t g_timing_stats;
volatile uint32_t g_timing_start_cycles = 0;
volatile uint32_t g_timing_case_id = HRT_TIMING_CASE_ID;
volatile uint32_t g_timing_event_hz = HRT_TIMING_EVENT_HZ;
volatile uint32_t g_timing_target_samples = HRT_TIMING_TARGET_SAMPLES;
volatile uint32_t g_example_error = 0;

/* Diagnostic PendSV exchange variables. These are global because the timing
   assembly handler references them directly. */
volatile uint32_t g_pendsv_measure_armed = 0;
volatile uint32_t g_pendsv_sample_ready = 0;
volatile uint32_t g_pendsv_entry_cycles = 0;
volatile uint32_t g_pendsv_save_cycles = 0;
volatile uint32_t g_pendsv_scheduler_cycles = 0;
volatile uint32_t g_pendsv_restore_cycles = 0;
volatile uint32_t g_pendsv_software_cycles = 0;

static volatile hrt_timing_stats_t g_pendsv_save_stats;
static volatile hrt_timing_stats_t g_pendsv_restore_stats;
static volatile hrt_timing_stats_t g_pendsv_software_stats;
static volatile hrt_timing_stats_t g_pendsv_to_task_stats;

static volatile uint32_t tim2_psc_dbg = 0;
static volatile uint32_t tim2_arr_dbg = 0;
static volatile hrt_timing_result_t g_timing_result;

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
void timing_target_reached(const volatile hrt_timing_result_t *result) {
    /* The qualification debugger breaks at function entry and receives the
       fixed-layout result record in r0. Keep the pointer semantically live so
       Release optimization cannot discard the call argument. */
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

__attribute__((noinline, used))
static void example_fail(uint32_t code) {
    g_example_error = code;
    __asm volatile("bkpt 0");
    for (;;) __asm volatile("wfi");
}

static void timing_finish(void) {
    const uint64_t sum = g_timing_stats.sum;

    g_timing_result.case_id = HRT_TIMING_CASE_ID;
    g_timing_result.event_hz = g_timing_event_hz;
    g_timing_result.target_samples = g_timing_target_samples;
    g_timing_result.core_hz = SystemCoreClock;
    g_timing_result.tim2_psc = tim2_psc_dbg;
    g_timing_result.tim2_arr = tim2_arr_dbg;
    g_timing_result.min_cycles = g_timing_stats.min;
    g_timing_result.avg_cycles = g_timing_stats.avg;
    g_timing_result.max_cycles = g_timing_stats.max;
    g_timing_result.count = g_timing_stats.count;
    g_timing_result.sum_lo = (uint32_t)sum;
    g_timing_result.sum_hi = (uint32_t)(sum >> 32u);
    g_timing_result.error = g_example_error;

#if HRT_TIMING_CASE_ID == HRT_TIMING_CASE_SCHEDULER_DECISION
    g_timing_result.pendsv_save_min = g_pendsv_save_stats.min;
    g_timing_result.pendsv_save_avg = g_pendsv_save_stats.avg;
    g_timing_result.pendsv_save_max = g_pendsv_save_stats.max;
    g_timing_result.pendsv_restore_min = g_pendsv_restore_stats.min;
    g_timing_result.pendsv_restore_avg = g_pendsv_restore_stats.avg;
    g_timing_result.pendsv_restore_max = g_pendsv_restore_stats.max;
    g_timing_result.pendsv_software_min = g_pendsv_software_stats.min;
    g_timing_result.pendsv_software_avg = g_pendsv_software_stats.avg;
    g_timing_result.pendsv_software_max = g_pendsv_software_stats.max;
    g_timing_result.pendsv_to_task_min = g_pendsv_to_task_stats.min;
    g_timing_result.pendsv_to_task_avg = g_pendsv_to_task_stats.avg;
    g_timing_result.pendsv_to_task_max = g_pendsv_to_task_stats.max;
#else
    g_timing_result.pendsv_save_min = 0u;
    g_timing_result.pendsv_save_avg = 0u;
    g_timing_result.pendsv_save_max = 0u;
    g_timing_result.pendsv_restore_min = 0u;
    g_timing_result.pendsv_restore_avg = 0u;
    g_timing_result.pendsv_restore_max = 0u;
    g_timing_result.pendsv_software_min = 0u;
    g_timing_result.pendsv_software_avg = 0u;
    g_timing_result.pendsv_software_max = 0u;
    g_timing_result.pendsv_to_task_min = 0u;
    g_timing_result.pendsv_to_task_avg = 0u;
    g_timing_result.pendsv_to_task_max = 0u;
#endif

    timing_target_reached(&g_timing_result);
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
#elif HRT_TIMING_CASE_ID == HRT_TIMING_CASE_SCHEDULER_DECISION
    g_pendsv_sample_ready = 0u;
    g_pendsv_measure_armed = 1u;
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
#elif HRT_TIMING_CASE_ID == HRT_TIMING_CASE_READY_TO_TASK
        {
            const uint32_t end = cycles_now();
            hrt_timing_stats_record(&g_timing_stats, end - g_timing_start_cycles);
        }
#elif HRT_TIMING_CASE_ID == HRT_TIMING_CASE_SCHEDULER_DECISION
        if (g_pendsv_sample_ready != 0u) {
            const uint32_t task_end = cycles_now();
            g_pendsv_sample_ready = 0u;

            hrt_timing_stats_record(&g_pendsv_save_stats, g_pendsv_save_cycles);
            hrt_timing_stats_record(&g_timing_stats, g_pendsv_scheduler_cycles);
            hrt_timing_stats_record(&g_pendsv_restore_stats, g_pendsv_restore_cycles);
            hrt_timing_stats_record(&g_pendsv_software_stats, g_pendsv_software_cycles);
            hrt_timing_stats_record(&g_pendsv_to_task_stats,
                                    task_end - g_pendsv_entry_cycles);
        }
#endif

        if (g_timing_stats.count >= g_timing_target_samples) {
            timing_finish();
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
    stats_init(&g_pendsv_save_stats);
    stats_init(&g_pendsv_restore_stats);
    stats_init(&g_pendsv_software_stats);
    stats_init(&g_pendsv_to_task_stats);
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
