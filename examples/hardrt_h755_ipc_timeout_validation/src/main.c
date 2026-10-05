#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"
#include "stm32h7xx.h"

#define STACK_WORDS 512u
#define EXTERNAL_TICK_HZ 1000u
#define TIMEOUT_TICKS 3u
#define SUCCESS_DEADLINE_TICKS 6u
#define HOLDER_SLEEP_MS 1000u

typedef struct {
    uint32_t passed;
    uint32_t error;
    uint32_t irq_count;
    uint32_t final_tick;
    uint32_t sem_success;
    uint32_t sem_success_tick;
    uint32_t sem_timeout;
    uint32_t queue_rx_timeout;
    uint32_t queue_tx_timeout;
    uint32_t mutex_timeout;
    uint32_t event_timeout;
    uint32_t notify_wait_timeout;
    uint32_t notify_take_timeout;
    uint32_t notify_recovery;
    uint32_t notify_take_recovery;
    uint32_t producer_runs;
} hrt_ipc_timeout_validation_result_t;

_Static_assert(sizeof(hrt_ipc_timeout_validation_result_t) == 64u,
               "ipc timeout validation result ABI");

static uint32_t stack_holder[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_validation[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stack_producer[STACK_WORDS] __attribute__((aligned(8)));

static hrt_sem_t g_sem;
static hrt_mutex_t g_mutex;
static hrt_queue_t g_queue;
static uint32_t g_queue_storage[1];
static hrt_event_t g_event;

static volatile hrt_ipc_timeout_validation_result_t g_result;
static volatile uint32_t g_irq_count;
static volatile uint32_t g_stage;
static volatile uint32_t g_producer_runs;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

__attribute__((noinline, used))
void ipc_timeout_validation_emit(
    const volatile hrt_ipc_timeout_validation_result_t *result)
{
    __asm volatile("" : : "r"(result) : "memory");
    for (;;) __asm volatile("wfi");
}

static void validation_stop(const uint32_t error)
{
    g_result.passed = (error == 0u) ? 1u : 0u;
    g_result.error = error;
    g_result.irq_count = g_irq_count;
    g_result.final_tick = hrt_tick_now();
    g_result.producer_runs = g_producer_runs;
    ipc_timeout_validation_emit(&g_result);
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
    g_irq_count++;
    hrt_tick_from_isr();
}

static void holder_task(void *arg)
{
    (void)arg;

    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0u) {
        validation_stop(101u);
    }

    tim2_start_external_tick();

    if (hrt_mutex_lock(&g_mutex) != 0) {
        validation_stop(102u);
    }

    /* Keep the mutex owned throughout the validation sequence. The validation
       task proves its timed mutex waiter expires/unlinks while this owner is
       sleeping in the same shared delta queue. */
    hrt_sleep(HOLDER_SLEEP_MS);
    validation_stop(103u);
}

static void producer_task(void *arg)
{
    (void)arg;

    while (g_stage == 0u) {
        hrt_yield();
    }

    /* Validation blocks first. Two external ticks later this producer gives
       the semaphore, proving producer wake disarms the deadline before expiry. */
    hrt_sleep(2u);
    g_producer_runs++;
    (void)hrt_sem_give(&g_sem);

    for (;;) {
        hrt_yield();
    }
}

static void validation_task(void *arg)
{
    (void)arg;

    /* 1. Producer wake before deadline: proves successful wake cancels timer. */
    const hrt_tick_t sem_success_deadline =
        hrt_tick_now() + SUCCESS_DEADLINE_TICKS;
    g_stage = 1u;
    g_result.sem_success =
        (uint32_t)hrt_sem_take_until(&g_sem, sem_success_deadline);
    g_result.sem_success_tick = hrt_tick_now();
    if (g_result.sem_success != (uint32_t)HRT_WAIT_OK) validation_stop(201u);
    if (g_result.sem_success_tick >= sem_success_deadline) validation_stop(202u);
    if (g_sem.count_wait != 0u) validation_stop(203u);

    /* 2. Semaphore expiry + waiter unlink. This also advances beyond the old
       success deadline, so a stale deadline from step 1 would be observable. */
    const hrt_tick_t sem_deadline = hrt_tick_now() + TIMEOUT_TICKS;
    g_result.sem_timeout = (uint32_t)hrt_sem_take_until(&g_sem, sem_deadline);
    if (g_result.sem_timeout != (uint32_t)HRT_WAIT_TIMEOUT) validation_stop(204u);
    if (hrt_tick_now() != sem_deadline) validation_stop(205u);
    if (g_sem.count_wait != 0u) validation_stop(206u);

    /* 3. Queue receive expiry. */
    hrt_queue_init(&g_queue, g_queue_storage, 1u, sizeof(g_queue_storage[0]));
    uint32_t value = 0u;
    const hrt_tick_t queue_rx_deadline = hrt_tick_now() + TIMEOUT_TICKS;
    g_result.queue_rx_timeout =
        (uint32_t)hrt_queue_recv_until(&g_queue, &value, queue_rx_deadline);
    if (g_result.queue_rx_timeout != (uint32_t)HRT_WAIT_TIMEOUT) validation_stop(207u);
    if (g_queue.rx_wait != 0u) validation_stop(208u);

    /* 4. Queue send expiry. */
    const uint32_t first = 0x11111111u;
    const uint32_t second = 0x22222222u;
    if (hrt_queue_try_send(&g_queue, &first) != 0) validation_stop(209u);
    const hrt_tick_t queue_tx_deadline = hrt_tick_now() + TIMEOUT_TICKS;
    g_result.queue_tx_timeout =
        (uint32_t)hrt_queue_send_until(&g_queue, &second, queue_tx_deadline);
    if (g_result.queue_tx_timeout != (uint32_t)HRT_WAIT_TIMEOUT) validation_stop(210u);
    if (g_queue.tx_wait != 0u) validation_stop(211u);
    value = 0u;
    if (hrt_queue_try_recv(&g_queue, &value) != 0 || value != first) {
        validation_stop(212u);
    }

    /* 5. Timed mutex expiry while holder remains asleep/owner. */
    const hrt_tick_t mutex_deadline = hrt_tick_now() + TIMEOUT_TICKS;
    g_result.mutex_timeout =
        (uint32_t)hrt_mutex_lock_until(&g_mutex, mutex_deadline);
    if (g_result.mutex_timeout != (uint32_t)HRT_WAIT_TIMEOUT) validation_stop(213u);
    if (g_mutex.count_wait != 0u) validation_stop(214u);

    /* 6. Event waiter expiry/unlink. */
    hrt_event_init(&g_event);
    const hrt_tick_t event_deadline = hrt_tick_now() + TIMEOUT_TICKS;
    g_result.event_timeout =
        (uint32_t)hrt_event_wait_until(&g_event, 0x1u, HRT_EVENT_WAIT_ANY,
                                       event_deadline, NULL);
    if (g_result.event_timeout != (uint32_t)HRT_WAIT_TIMEOUT) validation_stop(215u);
    if (g_event.wait_count != 0u) validation_stop(216u);

    /* 7. Notification-value expiry and recovery prove timeout clears the
       private notification waiter marker rather than poisoning later waits. */
    const hrt_tick_t notify_deadline = hrt_tick_now() + TIMEOUT_TICKS;
    value = 0u;
    g_result.notify_wait_timeout =
        (uint32_t)hrt_task_notify_wait_until(0u, 0u, notify_deadline, &value);
    if (g_result.notify_wait_timeout != (uint32_t)HRT_WAIT_TIMEOUT) validation_stop(217u);

    const int self = hrt__get_current();
    if (hrt_task_notify(self, 0x55u, HRT_NOTIFY_OVERWRITE) != 0) validation_stop(218u);
    value = 0u;
    g_result.notify_recovery =
        (uint32_t)hrt_task_notify_wait_until(0u, UINT32_MAX,
                                             hrt_tick_now(), &value);
    if (g_result.notify_recovery != (uint32_t)HRT_WAIT_OK || value != 0x55u) {
        validation_stop(219u);
    }

    /* 8. Counting-notification expiry and recovery. */
    const hrt_tick_t take_deadline = hrt_tick_now() + TIMEOUT_TICKS;
    value = 0u;
    g_result.notify_take_timeout =
        (uint32_t)hrt_task_notify_take_until(1, take_deadline, &value);
    if (g_result.notify_take_timeout != (uint32_t)HRT_WAIT_TIMEOUT) validation_stop(220u);

    if (hrt_task_notify(self, 0u, HRT_NOTIFY_INCREMENT) != 0) validation_stop(221u);
    value = 0u;
    g_result.notify_take_recovery =
        (uint32_t)hrt_task_notify_take_until(1, hrt_tick_now(), &value);
    if (g_result.notify_take_recovery != (uint32_t)HRT_WAIT_OK || value != 1u) {
        validation_stop(222u);
    }

    if (g_producer_runs != 1u) validation_stop(223u);
    validation_stop(0u);
}

int main(void)
{
    SystemInit();
    hold_cm4();

    hrt_sem_init(&g_sem, 0u);
    hrt_mutex_init(&g_mutex);
    hrt_queue_init(&g_queue, g_queue_storage, 1u, sizeof(g_queue_storage[0]));
    hrt_event_init(&g_event);

    const hrt_config_t cfg = {
        .tick_hz = EXTERNAL_TICK_HZ,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0u,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_EXTERNAL
    };
    if (hrt_init(&cfg) != HRT_OK) validation_stop(1u);

    const hrt_task_attr_t holder_attr = {
        .priority = HRT_PRIO0,
        .timeslice = 0u
    };
    const hrt_task_attr_t validation_attr = {
        .priority = HRT_PRIO1,
        .timeslice = 0u
    };
    const hrt_task_attr_t producer_attr = {
        .priority = HRT_PRIO2,
        .timeslice = 0u
    };

    if (hrt_create_task(holder_task, NULL, stack_holder, STACK_WORDS,
                        &holder_attr) < 0) {
        validation_stop(2u);
    }
    if (hrt_create_task(validation_task, NULL, stack_validation, STACK_WORDS,
                        &validation_attr) < 0) {
        validation_stop(3u);
    }
    if (hrt_create_task(producer_task, NULL, stack_producer, STACK_WORDS,
                        &producer_attr) < 0) {
        validation_stop(4u);
    }

    hrt_start();
    validation_stop(5u);
    return 1;
}
