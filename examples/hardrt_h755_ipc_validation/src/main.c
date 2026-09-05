#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"
#include "hardrt_event.h"
#include "hardrt_mutex.h"
#include "hardrt_notify.h"
#include "hardrt_queue.h"
#include "hardrt_sem.h"
#include "hardrt_time.h"
#include "stm32h7xx.h"

#ifndef HRT_IPC_CASE_ID
#define HRT_IPC_CASE_ID 1
#endif

#if HRT_IPC_CASE_ID < 1 || HRT_IPC_CASE_ID > 5
#error "HRT_IPC_CASE_ID must be 1 (semaphore), 2 (queue), 3 (mutex), 4 (event), or 5 (notification)"
#endif

#define STACK_WORDS 512u
#define IRQ_DELAY_US 3000u
#define QUEUE_VALUE_A 0x13579BDFu
#define QUEUE_VALUE_B 0x2468ACE0u
#define EVENT_BIT_A 0x00000001u
#define EVENT_BIT_B 0x00000002u
#define EVENT_BIT_ANY 0x00000008u
#define NOTIFY_VALUE_PRE 0x00000011u
#define NOTIFY_VALUE_BITS 0x00000004u
#define NOTIFY_VALUE_ISR 0x00000020u

typedef struct {
    uint32_t case_id;
    uint32_t passed;
    uint32_t error;
    uint32_t irq_count;
    int32_t need_switch_0;
    int32_t need_switch_1;
    uint32_t observed_0;
    uint32_t observed_1;
    uint32_t sequence[8];
} hrt_ipc_result_t;

_Static_assert(offsetof(hrt_ipc_result_t, case_id) == 0u, "ipc result ABI");
_Static_assert(offsetof(hrt_ipc_result_t, need_switch_0) == 16u, "ipc result ABI");
_Static_assert(offsetof(hrt_ipc_result_t, sequence) == 32u, "ipc result ABI");
_Static_assert(sizeof(hrt_ipc_result_t) == 64u, "ipc result ABI");

static uint32_t stack_high[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_low[STACK_WORDS] __attribute__((aligned(8)));

static hrt_sem_t g_gate;
static hrt_sem_t g_sem;
static hrt_mutex_t g_mutex;
static hrt_queue_t g_queue;
static uint32_t g_queue_storage[1];
static hrt_event_t g_event;

static volatile hrt_ipc_result_t g_validation_result;
volatile uint32_t g_example_error = 0u;
volatile uint32_t g_irq_count = 0u;
volatile int32_t g_need_switch_0 = -1;
volatile int32_t g_need_switch_1 = -1;
volatile uint32_t g_observed_0 = 0u;
volatile uint32_t g_observed_1 = 0u;
volatile uint32_t g_sequence[8] = {0u};

static volatile uint32_t g_phase = 0u;
static volatile uint32_t g_high_done = 0u;
static volatile uint32_t g_irq_mode = 0u;
static volatile uint32_t g_isr_queue_value = 0u;
static volatile int32_t g_high_task_id = -1;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

__attribute__((noinline, used))
void ipc_validation_emit(const volatile hrt_ipc_result_t *result)
{
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

__attribute__((noinline))
static void validation_stop(uint32_t error, uint32_t passed)
{
    g_example_error = error;
    g_validation_result.case_id = HRT_IPC_CASE_ID;
    g_validation_result.passed = passed;
    g_validation_result.error = error;
    g_validation_result.irq_count = g_irq_count;
    g_validation_result.need_switch_0 = g_need_switch_0;
    g_validation_result.need_switch_1 = g_need_switch_1;
    g_validation_result.observed_0 = g_observed_0;
    g_validation_result.observed_1 = g_observed_1;
    for (unsigned i = 0u; i < 8u; ++i) g_validation_result.sequence[i] = g_sequence[i];
    ipc_validation_emit(&g_validation_result);
}

static void validation_fail(uint32_t error) { validation_stop(error, 0u); }
static void validation_pass(void) { validation_stop(0u, 1u); }

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

#if HRT_IPC_CASE_ID == 1
    g_sequence[1] = 2u;
    int need_switch = 0;
    (void)hrt_sem_give_from_isr(&g_sem, &need_switch);
    g_need_switch_0 = need_switch;
#elif HRT_IPC_CASE_ID == 2
    if (g_irq_mode == 1u) {
        g_sequence[1] = 2u;
        uint32_t value = QUEUE_VALUE_A;
        int need_switch = 0;
        if (hrt_queue_try_send_from_isr(&g_queue, &value, &need_switch) != 0) validation_fail(210u);
        g_need_switch_0 = need_switch;
    } else if (g_irq_mode == 2u) {
        g_sequence[5] = 6u;
        uint32_t value = 0u;
        int need_switch = 0;
        if (hrt_queue_try_recv_from_isr(&g_queue, &value, &need_switch) != 0) validation_fail(211u);
        g_isr_queue_value = value;
        g_need_switch_1 = need_switch;
    } else {
        validation_fail(212u);
    }
#elif HRT_IPC_CASE_ID == 3
    validation_fail(310u);
#elif HRT_IPC_CASE_ID == 4
    g_sequence[1] = 2u;
    int need_switch = 0;
    if (hrt_event_set_from_isr(&g_event, EVENT_BIT_B, &need_switch) != 0) validation_fail(410u);
    g_need_switch_0 = need_switch;
#elif HRT_IPC_CASE_ID == 5
    g_sequence[3] = 4u;
    int need_switch = 0;
    if (g_high_task_id < 0 ||
        hrt_task_notify_from_isr((int)g_high_task_id,
                                 NOTIFY_VALUE_ISR,
                                 HRT_NOTIFY_SET_BITS,
                                 &need_switch) != 0) {
        validation_fail(510u);
    }
    g_need_switch_0 = need_switch;
#endif
}

#if HRT_IPC_CASE_ID == 1
static void high_task(void *arg)
{
    (void)arg;
    if (hrt_sem_take(&g_sem) != 0) validation_fail(110u);
    g_sequence[2] = 3u;
    if (g_irq_count != 1u || g_need_switch_0 != 1) validation_fail(111u);
    g_high_done = 1u;
    hrt_sleep(1000u);
    validation_fail(112u);
}

static void low_task(void *arg)
{
    (void)arg;
    g_sequence[0] = 1u;
    tim2_start_one_shot_us(IRQ_DELAY_US);
    while (g_irq_count == 0u) { }
    if (g_high_done == 0u) validation_fail(113u);
    g_sequence[3] = 4u;
    validation_pass();
}

static void pre_scheduler_api_checks(void)
{
    hrt_sem_t c;
    hrt_sem_init_counting(&c, 7u, 2u);
    if (hrt_sem_try_take(&c) != 0) validation_fail(101u);
    if (hrt_sem_try_take(&c) != 0) validation_fail(102u);
    if (hrt_sem_try_take(&c) == 0) validation_fail(103u);
    (void)hrt_sem_give(&c);
    (void)hrt_sem_give(&c);
    (void)hrt_sem_give(&c);
    if (hrt_sem_try_take(&c) != 0) validation_fail(104u);
    if (hrt_sem_try_take(&c) != 0) validation_fail(105u);
    if (hrt_sem_try_take(&c) == 0) validation_fail(106u);
}
#endif

#if HRT_IPC_CASE_ID == 2
static void high_task(void *arg)
{
    (void)arg;
    uint32_t value = 0u;
    if (hrt_queue_recv(&g_queue, &value) != 0) validation_fail(220u);
    g_sequence[2] = 3u;
    if (value != QUEUE_VALUE_A || g_need_switch_0 != 1) validation_fail(221u);

    value = QUEUE_VALUE_A;
    if (hrt_queue_try_send(&g_queue, &value) != 0) validation_fail(222u);
    g_phase = 1u;
    value = QUEUE_VALUE_B;
    if (hrt_queue_send(&g_queue, &value) != 0) validation_fail(223u);

    g_sequence[6] = 7u;
    if (g_isr_queue_value != QUEUE_VALUE_A || g_need_switch_1 != 1) validation_fail(224u);
    value = 0u;
    if (hrt_queue_try_recv(&g_queue, &value) != 0 || value != QUEUE_VALUE_B) validation_fail(225u);
    g_observed_0 = QUEUE_VALUE_A;
    g_observed_1 = value;
    g_high_done = 1u;
    hrt_sleep(1000u);
    validation_fail(226u);
}

static void low_task(void *arg)
{
    (void)arg;
    g_sequence[0] = 1u;
    g_irq_mode = 1u;
    tim2_start_one_shot_us(IRQ_DELAY_US);

    while (g_phase == 0u) {
        if (g_irq_count != 0u && g_sequence[2] != 3u) validation_fail(230u);
    }

    g_sequence[3] = 4u;
    g_sequence[4] = 5u;
    g_irq_mode = 2u;
    tim2_start_one_shot_us(IRQ_DELAY_US);

    while (g_high_done == 0u) {
        if (g_irq_count >= 2u && g_sequence[6] != 7u) validation_fail(231u);
    }
    g_sequence[7] = 8u;
    validation_pass();
}

static void pre_scheduler_api_checks(void)
{
    hrt_queue_t q;
    uint32_t storage[2] = {0u, 0u};
    uint32_t a = 11u, b = 22u, out = 0u;
    hrt_queue_init(&q, storage, 2u, sizeof(uint32_t));
    if (hrt_queue_try_recv(&q, &out) == 0) validation_fail(201u);
    if (hrt_queue_try_send(&q, &a) != 0) validation_fail(202u);
    if (hrt_queue_try_send(&q, &b) != 0) validation_fail(203u);
    if (hrt_queue_try_send(&q, &a) == 0) validation_fail(204u);
    if (hrt_queue_count(&q) != 2u) validation_fail(205u);
    if (hrt_queue_try_recv(&q, &out) != 0 || out != a) validation_fail(206u);
    if (hrt_queue_try_recv(&q, &out) != 0 || out != b) validation_fail(207u);
    if (hrt_queue_try_recv(&q, &out) == 0) validation_fail(208u);
}
#endif

#if HRT_IPC_CASE_ID == 3
static void high_task(void *arg)
{
    (void)arg;
    if (hrt_sem_take(&g_gate) != 0) validation_fail(320u);
    g_sequence[1] = 2u;
    if (hrt_mutex_lock(&g_mutex) != 0) validation_fail(321u);
    g_sequence[3] = 4u;
    g_observed_0 = (uint32_t)g_mutex.owner;
    if (hrt_mutex_unlock(&g_mutex) != 0) validation_fail(322u);
    g_high_done = 1u;
    hrt_sleep(1000u);
    validation_fail(323u);
}

static void low_task(void *arg)
{
    (void)arg;
    if (hrt_mutex_lock(&g_mutex) != 0) validation_fail(330u);
    g_sequence[0] = 1u;
    (void)hrt_sem_give(&g_gate);

    g_sequence[2] = 3u;
    if (g_mutex.owner == HRT_MUTEX_NO_OWNER) validation_fail(331u);
    if (hrt_mutex_unlock(&g_mutex) != 0) validation_fail(332u);
    if (g_high_done == 0u) validation_fail(333u);
    g_sequence[4] = 5u;
    g_observed_1 = (uint32_t)g_mutex.owner;
    if (g_mutex.locked != 0u || g_mutex.owner != HRT_MUTEX_NO_OWNER) validation_fail(334u);
    validation_pass();
}

static void pre_scheduler_api_checks(void) { }
#endif

#if HRT_IPC_CASE_ID == 4
static void high_task(void *arg)
{
    (void)arg;
    hrt_event_bits_t matched = 0u;
    const unsigned wait_all_clear =
        (unsigned)HRT_EVENT_WAIT_ALL | (unsigned)HRT_EVENT_CLEAR_ON_EXIT;

    if (hrt_event_wait(&g_event,
                       EVENT_BIT_A | EVENT_BIT_B,
                       wait_all_clear,
                       &matched) != 0) {
        validation_fail(420u);
    }
    g_sequence[2] = 3u;
    g_observed_0 = matched;
    if (matched != (EVENT_BIT_A | EVENT_BIT_B) ||
        g_irq_count != 1u || g_need_switch_0 != 1 ||
        hrt_event_get(&g_event) != 0u) {
        validation_fail(421u);
    }

    g_phase = 1u;
    g_sequence[3] = 4u;
    matched = 0u;
    if (hrt_event_wait(&g_event,
                       EVENT_BIT_ANY | 0x00000004u,
                       (unsigned)HRT_EVENT_WAIT_ANY,
                       &matched) != 0) {
        validation_fail(422u);
    }
    g_sequence[5] = 6u;
    g_observed_1 = matched;
    if (matched != EVENT_BIT_ANY ||
        (hrt_event_get(&g_event) & EVENT_BIT_ANY) == 0u) {
        validation_fail(423u);
    }
    if (hrt_event_clear(&g_event, EVENT_BIT_ANY) != 0 ||
        hrt_event_get(&g_event) != 0u) {
        validation_fail(424u);
    }

    g_high_done = 1u;
    hrt_sleep(1000u);
    validation_fail(425u);
}

static void low_task(void *arg)
{
    (void)arg;
    g_sequence[0] = 1u;
    if (hrt_event_set(&g_event, EVENT_BIT_A) != 0) validation_fail(430u);
    if (hrt_event_get(&g_event) != EVENT_BIT_A || g_high_done != 0u) validation_fail(431u);

    tim2_start_one_shot_us(IRQ_DELAY_US);
    while (g_phase == 0u) {
        if (g_irq_count != 0u && g_sequence[2] != 3u) validation_fail(432u);
    }

    g_sequence[4] = 5u;
    if (hrt_event_set(&g_event, EVENT_BIT_ANY) != 0) validation_fail(433u);
    if (g_high_done == 0u) validation_fail(434u);
    g_sequence[6] = 7u;
    validation_pass();
}

static void pre_scheduler_api_checks(void)
{
    hrt_event_t e;
    hrt_event_init(&e);
    if (hrt_event_get(&e) != 0u) validation_fail(401u);
    if (hrt_event_set(&e, 0x5u) != 0 || hrt_event_get(&e) != 0x5u) validation_fail(402u);
    if (hrt_event_clear(&e, 0x1u) != 0 || hrt_event_get(&e) != 0x4u) validation_fail(403u);
}
#endif

#if HRT_IPC_CASE_ID == 5
static void high_task(void *arg)
{
    (void)arg;
    uint32_t value = 0u;

    if (hrt_sem_take(&g_gate) != 0) validation_fail(520u);
    g_sequence[1] = 2u;
    if (hrt_task_notify_wait(0u, UINT32_MAX, &value) != 0) validation_fail(521u);
    g_observed_0 = value;
    if (value != (NOTIFY_VALUE_PRE | NOTIFY_VALUE_BITS)) validation_fail(522u);

    g_phase = 1u;
    g_sequence[2] = 3u;
    value = 0u;
    if (hrt_task_notify_wait(0u, UINT32_MAX, &value) != 0) validation_fail(523u);
    g_sequence[4] = 5u;
    g_observed_1 = value;
    if (value != NOTIFY_VALUE_ISR || g_irq_count != 1u || g_need_switch_0 != 1) validation_fail(524u);

    g_phase = 2u;
    g_sequence[5] = 6u;
    if (hrt_sem_take(&g_gate) != 0) validation_fail(525u);
    const uint32_t first = hrt_task_notify_take(0);
    const uint32_t second = hrt_task_notify_take(1);
    if (first != 2u || second != 1u) validation_fail(526u);
    g_sequence[7] = 8u;
    g_high_done = 1u;
    hrt_sleep(1000u);
    validation_fail(527u);
}

static void low_task(void *arg)
{
    (void)arg;
    g_sequence[0] = 1u;
    if (g_high_task_id < 0) validation_fail(530u);
    if (hrt_task_notify((int)g_high_task_id,
                        NOTIFY_VALUE_PRE,
                        HRT_NOTIFY_OVERWRITE) != 0) {
        validation_fail(531u);
    }
    if (hrt_task_notify((int)g_high_task_id,
                        0x00000022u,
                        HRT_NOTIFY_NO_OVERWRITE) == 0) {
        validation_fail(532u);
    }
    if (hrt_task_notify((int)g_high_task_id,
                        NOTIFY_VALUE_BITS,
                        HRT_NOTIFY_SET_BITS) != 0) {
        validation_fail(533u);
    }
    if (g_phase != 0u) validation_fail(534u);
    if (hrt_sem_give(&g_gate) != 0) validation_fail(535u);

    while (g_phase < 1u) { }
    tim2_start_one_shot_us(IRQ_DELAY_US);
    while (g_phase < 2u) {
        if (g_irq_count != 0u && g_sequence[4] != 5u) validation_fail(536u);
    }

    if (hrt_task_notify((int)g_high_task_id, 0u, HRT_NOTIFY_INCREMENT) != 0) validation_fail(537u);
    if (hrt_task_notify((int)g_high_task_id, 0u, HRT_NOTIFY_INCREMENT) != 0) validation_fail(538u);
    g_sequence[6] = 7u;
    if (hrt_sem_give(&g_gate) != 0) validation_fail(539u);

    while (g_high_done == 0u) { }
    validation_pass();
}

static void pre_scheduler_api_checks(void)
{
    if (hrt_task_notify(-1, 1u, HRT_NOTIFY_OVERWRITE) == 0) validation_fail(501u);
    if (hrt_task_notify(0, 1u, HRT_NOTIFY_OVERWRITE) == 0) validation_fail(502u);
}
#endif

int main(void)
{
    SystemInit();
    hold_cm4();

    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0u,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };
    if (hrt_init(&cfg) != 0) validation_fail(1u);

    hrt_sem_init(&g_gate, 0u);
    hrt_sem_init(&g_sem, 0u);
    hrt_mutex_init(&g_mutex);
    hrt_queue_init(&g_queue, g_queue_storage, 1u, sizeof(uint32_t));
    hrt_event_init(&g_event);
    pre_scheduler_api_checks();

    const hrt_task_attr_t high_attr = {.priority = HRT_PRIO0, .timeslice = 0u};
    const hrt_task_attr_t low_attr = {.priority = HRT_PRIO1, .timeslice = 0u};
    const int high_id = hrt_create_task(high_task, 0, stack_high, STACK_WORDS, &high_attr);
    if (high_id < 0) validation_fail(2u);
    g_high_task_id = high_id;
    if (hrt_create_task(low_task, 0, stack_low, STACK_WORDS, &low_attr) < 0) validation_fail(3u);

    hrt_start();
    validation_fail(4u);
    return 1;
}
