/*
 * main.c - HardRT timing test (STM32H755, M7 core)
 *
 * Measures:
 *   1) External tick ISR -> TaskA execution (tick wake)
 *   2) Event ISR (TIM3) sem_give_from_isr -> TaskCritical after sem_take returns
 *
 * Adds "arming" flags so each sample is paired to the correct event.
 */

#include <stdint.h>
#include <stdbool.h>

#include "hardrt.h"
#include "hardrt_sem.h"
#include "hardrt_time.h"
#include "stm32h7xx.h"

/* -------------------- DWT cycle counter -------------------- */
#define DEMCR      (*(volatile uint32_t*)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004u)

static inline void dwt_init(void) {
    DEMCR |= (1u << 24);     /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;          /* CYCCNTENA */
}

static inline uint32_t cycles_now(void) {
    return DWT_CYCCNT;
}

__attribute__((noinline)) void timing_target_reached(void)
{
    __asm volatile("bkpt 0");
}

/* -------------------- Targets -------------------- */
#define TICK_TARGET_SAMPLES 1000u
#define SEM_TARGET_SAMPLES  1000u

/* -------------------- Stats helper -------------------- */
typedef struct {
    volatile uint32_t min;
    volatile uint32_t max;
    volatile uint32_t avg;
    volatile uint32_t count;
    volatile uint64_t sum;
} hrt_stats_t;

static inline void stats_init(hrt_stats_t* s)
{
    s->min = 0xFFFFFFFFu;
    s->max = 0u;
    s->avg = 0u;
    s->count = 0u;
    s->sum = 0u;
}

static inline void stats_record(hrt_stats_t* s, uint32_t v)
{
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->sum += v;
    s->count++;
    s->avg = (uint32_t)(s->sum / s->count);
}

/* -------------------- Separate statistics -------------------- */
static hrt_stats_t g_tick_stats;
static hrt_stats_t g_sem_stats;

static volatile uint32_t g_tick_done = 0;
static volatile uint32_t g_sem_done  = 0;

static inline void maybe_finish(void)
{
    if (g_tick_done && g_sem_done) {
        timing_target_reached();
    }
}

/* -------------------- Hold CM4 in reset (H755 dual-core) -------------------- */
static inline void hold_cm4(void){
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t*)(RCC_BASE_NEW + 0x0))
#define RCC_GRSTCSETR  (*(volatile uint32_t*)(RCC_BASE_NEW + 0x8))
    RCC_GCR &= ~(1u<<0);
    RCC_GRSTCSETR = (1u<<0);
}

/* -------------------- External symbols -------------------- */
extern void SystemInit(void);
extern uint32_t SystemCoreClock;

/* -------------------- Tasks and stacks -------------------- */
#define STACK_WORDS 1024
static uint32_t stackA[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stackB[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stackC[STACK_WORDS] __attribute__((aligned(8)));

volatile uint32_t dbg_counterA = 0;
volatile uint32_t dbg_counterB = 0;
volatile uint32_t dbg_counterC = 0;

static hrt_sem_t sem_evt;

/* -------------------- Arming + timestamps -------------------- */
/* Tick->Task */
static volatile uint32_t t0_tick = 0;
static volatile uint32_t tick_armed = 0;

/* Sem->Task */
static volatile uint32_t t0_sem = 0;
static volatile uint32_t sem_armed = 0;

/* Gate ISR activity until kernel is live */
static volatile uint32_t g_irqs_enabled = 0;

/* -------------------- TaskA: Tick -> task wake -------------------- */
static void TaskA(void* arg)
{
    (void)arg;
    g_irqs_enabled = 1;

    //todo measures period instead of tick -> task
    // needs fix
    for (;;) {
        hrt_sleep(1);

        /* Only record if armed, and clear immediately */
        if (tick_armed) {
            uint32_t t1 = cycles_now();
            uint32_t d  = t1 - t0_tick;
            tick_armed = 0;

            stats_record(&g_tick_stats, d);
            if (g_tick_stats.count >= TICK_TARGET_SAMPLES) {
                g_tick_done = 1;
                maybe_finish();
            }
        }

        dbg_counterA++;
        /* REMOVE extra sleep here */
    }
}


/* -------------------- TaskB: background load -------------------- */
static void TaskB(void* arg)
{
    (void)arg;
    for (;;) {
        dbg_counterB++;
        hrt_sleep(50);
    }
}

/* -------------------- TaskCritical: Sem give -> take -------------------- */
static void TaskCritical(void* arg)
{
    (void)arg;
    for (;;) {
        hrt_sem_take(&sem_evt);

        /* Only record if ISR armed it (pairs sample to event) */
        if (sem_armed) {
            uint32_t t1 = cycles_now();
            uint32_t d  = t1 - t0_sem;

            sem_armed = 0;
            stats_record(&g_sem_stats, d);

            if (g_sem_stats.count >= SEM_TARGET_SAMPLES) {
                g_sem_done = 1;
                maybe_finish();
            }
        }

        dbg_counterC++;
    }
}

/* -------------------- TIM2: External tick source -------------------- */
static void tim2_init_tick(uint32_t tick_hz)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    __asm volatile ("dsb 0xF" ::: "memory");

    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM2RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_TIM2RST;

    uint32_t tim_clk = SystemCoreClock / 2u; /* rough; ok for periodic interrupt */
    uint32_t psc = (tim_clk / 1000000u);
    if (psc == 0) psc = 1;
    psc -= 1u;

    if (tick_hz == 0) tick_hz = 1000;
    uint32_t arr = (1000000u / tick_hz);
    if (arr == 0) arr = 1;
    arr -= 1u;

    TIM2->PSC = psc;
    TIM2->ARR = arr;

    TIM2->EGR  = TIM_EGR_UG;
    TIM2->SR   = 0;
    TIM2->DIER = TIM_DIER_UIE;
    TIM2->CR1  = TIM_CR1_CEN;

    /* Must be RTOS-callable priority (low urgency). */
    NVIC_SetPriority(TIM2_IRQn, 12);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR &= ~TIM_SR_UIF;

        if (!g_irqs_enabled) {
            return;
        }

        /* Arm tick timestamp only if we haven't already armed one */
        if (!tick_armed && (g_tick_stats.count < TICK_TARGET_SAMPLES)) {
            t0_tick = cycles_now();
            tick_armed = 1;
        }


        /* External tick driver */
        hrt_tick_from_isr();
    }
}

/* -------------------- TIM3: Event producer -> semaphore give -------------------- */
static void tim3_init_event(uint32_t hz)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM3EN;
    __asm volatile ("dsb 0xF" ::: "memory");

    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM3RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_TIM3RST;

    uint32_t tim_clk = SystemCoreClock / 2u;
    uint32_t psc = (tim_clk / 1000000u);
    if (psc == 0) psc = 1;
    psc -= 1u;

    if (hz == 0) hz = 200;
    uint32_t arr = (1000000u / hz);
    if (arr == 0) arr = 1;
    arr -= 1u;

    TIM3->PSC = psc;
    TIM3->ARR = arr;

    TIM3->EGR  = TIM_EGR_UG;
    TIM3->SR   = 0;
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CR1  = TIM_CR1_CEN;

    NVIC_SetPriority(TIM3_IRQn, 12);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);
}

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF) {
        TIM3->SR &= ~TIM_SR_UIF;

        if (!g_irqs_enabled) {
            return;
        }

        if (!sem_armed && (g_sem_stats.count < SEM_TARGET_SAMPLES)) {
            t0_sem = cycles_now();
            sem_armed = 1;
        }

        int should_switch = 0;
        hrt_sem_give_from_isr(&sem_evt, &should_switch);
        (void)should_switch;
    }
}

/* -------------------- main -------------------- */
int main(void)
{
    SystemInit();
    hold_cm4();
    dwt_init();

    stats_init(&g_tick_stats);
    stats_init(&g_sem_stats);

    const uint32_t tick_hz = 1000;
    const uint32_t evt_hz  = 200;

    hrt_config_t cfg = {
        .tick_hz        = tick_hz,
        .policy         = HRT_SCHED_PRIORITY_RR,
        .default_slice  = 5,
        .core_hz        = SystemCoreClock,
        .tick_src       = HRT_TICK_EXTERNAL
    };

    hrt_init(&cfg);

    hrt_sem_init(&sem_evt, 0);

    /* Make TaskCritical highest if you want "asap on event" behavior */
    hrt_task_attr_t aA = { .priority = HRT_PRIO1, .timeslice = 5 };
    hrt_task_attr_t aB = { .priority = HRT_PRIO1, .timeslice = 5 };
    hrt_task_attr_t aC = { .priority = HRT_PRIO0, .timeslice = 5 };

    hrt_create_task(TaskA, 0, stackA, (uint32_t)(sizeof(stackA)/sizeof(stackA[0])), &aA);
    hrt_create_task(TaskB, 0, stackB, (uint32_t)(sizeof(stackB)/sizeof(stackB[0])), &aB);
    hrt_create_task(TaskCritical, 0, stackC, (uint32_t)(sizeof(stackC)/sizeof(stackC[0])), &aC);

    /* Start interrupt sources after kernel init; ISR gates on g_irqs_enabled anyway */
    tim2_init_tick(tick_hz);
    tim3_init_event(evt_hz);

    hrt_start();
    return 0;
}
