#include <stdint.h>
#include <stdbool.h>

#include "hardrt.h"
#include "hardrt_sem.h"
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
static inline uint32_t cycles_now(void) { return DWT_CYCCNT; }

__attribute__((noinline)) void timing_target_reached(void) {
    __asm volatile("bkpt 0");
}

/* -------------------- Targets -------------------- */
#define TICK_TARGET_SAMPLES 10000u
#define EVT_TARGET_SAMPLES  10000u

/* -------------------- Stats struct (same layout your GDB script assumes) -------------------- */
typedef struct {
    volatile uint32_t min;   /* +0 */
    volatile uint32_t max;   /* +4 */
    volatile uint32_t avg;   /* +8 */
    volatile uint32_t count; /* +12 */
    volatile uint64_t sum;   /* +16 */
} hrt_stats_t;

/* Make them GLOBAL (not static) so GDB sees them as symbols reliably */
volatile hrt_stats_t g_tick_stats;
volatile hrt_stats_t g_sem_stats;

static inline void stats_init(volatile hrt_stats_t* s) {
    s->min = 0xFFFFFFFFu;
    s->max = 0u;
    s->avg = 0u;
    s->count = 0u;
    s->sum = 0u;
}
static inline void stats_record(volatile hrt_stats_t* s, uint32_t v) {
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->sum += v;
    s->count++;
    s->avg = (uint32_t)(s->sum / s->count);
}

/* -------------------- Completion flags -------------------- */
static volatile uint32_t g_tick_done = 0;
static volatile uint32_t g_evt_done  = 0;

static inline void maybe_finish(void) {
    if (g_tick_done && g_evt_done) timing_target_reached();
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

/* -------------------- Debug-export timer config (force symbol retention) -------------------- */
volatile uint32_t tim2_psc_dbg __attribute__((used)) = 0;
volatile uint32_t tim2_arr_dbg __attribute__((used)) = 0;
volatile uint32_t tim3_psc_dbg __attribute__((used)) = 0;
volatile uint32_t tim3_arr_dbg __attribute__((used)) = 0;

/* -------------------- Semaphores -------------------- */
static hrt_sem_t sem_tick;
static hrt_sem_t sem_evt;

/* -------------------- Arming + timestamps -------------------- */
static volatile uint32_t t0_tick = 0;
static volatile uint32_t t0_evt  = 0;
static volatile uint32_t tick_armed = 0;
static volatile uint32_t evt_armed  = 0;

/* Gate interrupts until tasks are alive */
static volatile uint32_t g_irqs_enabled = 0;

/* -------------------- Tasks and stacks -------------------- */
#define STACK_WORDS 1024
static uint32_t stackTick[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stackEvt[STACK_WORDS]  __attribute__((aligned(8)));

static void TaskTickLatency(void* arg)
{
    (void)arg;
    g_irqs_enabled = 1;

    for (;;) {
        hrt_sem_take(&sem_tick);

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
    }
}

static void TaskEventLatency(void* arg)
{
    (void)arg;

    for (;;) {
        hrt_sem_take(&sem_evt);

        if (evt_armed) {
            uint32_t t1 = cycles_now();
            uint32_t d  = t1 - t0_evt;
            evt_armed = 0;

            stats_record(&g_sem_stats, d);

            if (g_sem_stats.count >= EVT_TARGET_SAMPLES) {
                g_evt_done = 1;
                maybe_finish();
            }
        }
    }
}

/* -------------------- TIM2: "Tick event" producer -------------------- */
/* This is NOT the RTOS tick. It's just a periodic interrupt used as an event source.
   If you also run HardRT external tick mode, keep that separate. */
static void tim2_init_event(uint32_t hz)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN;
    __asm volatile ("dsb 0xF" ::: "memory");

    RCC->APB1LRSTR |= RCC_APB1LRSTR_TIM2RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_TIM2RST;

    /* Keep your "rough" clock for now, but expose PSC/ARR so you can confirm actual rate */
    uint32_t tim_clk = SystemCoreClock / 2u;
    uint32_t psc = (tim_clk / 1000000u);
    if (psc == 0) psc = 1;
    psc -= 1u;

    if (hz == 0) hz = 1000;
    uint32_t arr = (1000000u / hz);
    if (arr == 0) arr = 1;
    arr -= 1u;

    TIM2->PSC = psc;
    TIM2->ARR = arr;

    tim2_psc_dbg = psc;
    tim2_arr_dbg = arr;

    TIM2->EGR  = TIM_EGR_UG;
    TIM2->SR   = 0;
    TIM2->DIER = TIM_DIER_UIE;
    TIM2->CR1  = TIM_CR1_CEN;

    NVIC_SetPriority(TIM2_IRQn, 12);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR &= ~TIM_SR_UIF;

        if (!g_irqs_enabled) return;
        if (g_tick_stats.count >= TICK_TARGET_SAMPLES) return;

        if (!tick_armed) {
            t0_tick = cycles_now();
            tick_armed = 1;
        }

        int should_switch = 0;
        hrt_sem_give_from_isr(&sem_tick, &should_switch);
        (void)should_switch;
    }
}

/* -------------------- TIM3: external event producer -------------------- */
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

    tim3_psc_dbg = psc;
    tim3_arr_dbg = arr;

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

        if (!g_irqs_enabled) return;
        if (g_sem_stats.count >= EVT_TARGET_SAMPLES) return;

        if (!evt_armed) {
            t0_evt = cycles_now();
            evt_armed = 1;
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

    /* Keep your kernel tick config as-is (internal or external).
       This test measures event->task, independent of how time advances. */
    hrt_config_t cfg = {
        .tick_hz        = 1000,
        .policy         = HRT_SCHED_PRIORITY_RR,
        .default_slice  = 5,
        .core_hz        = SystemCoreClock,
        .tick_src       = HRT_TICK_SYSTICK   /* or HRT_TICK_EXTERNAL if you want */
    };

    hrt_init(&cfg);

    hrt_sem_init(&sem_tick, 0);
    hrt_sem_init(&sem_evt, 0);

    /* Latency tasks should be highest priority */
    hrt_task_attr_t p_tick = { .priority = HRT_PRIO0, .timeslice = 0 };
    hrt_task_attr_t p_evt  = { .priority = HRT_PRIO0, .timeslice = 0 };

    hrt_create_task(TaskTickLatency, 0, stackTick, (uint32_t)(sizeof(stackTick)/sizeof(stackTick[0])), &p_tick);
    hrt_create_task(TaskEventLatency, 0, stackEvt, (uint32_t)(sizeof(stackEvt)/sizeof(stackEvt[0])), &p_evt);

    /* Start event sources */
    tim2_init_event(1000);  /* "tick-like" event at 1 kHz */
    tim3_init_event(200);   /* slower event */

    hrt_start();
    return 0;
}
