/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include "hardrt.h"
#include "stm32h7xx.h"

#define STACK_WORDS 512u
#define REQUIRED_FLAGS 0xFFu

typedef struct {
    uint32_t passed;
    uint32_t error;
    uint32_t flags;
    uint32_t competitor_before_handoff;
    uint32_t competitor_total;
    uint32_t low_id;
    uint32_t middle_id;
    uint32_t high_id;
    uint32_t lock_a_owner;
    uint32_t lock_b_owner;
} hrt_mutex_pi_validation_result_t;

_Static_assert(sizeof(hrt_mutex_pi_validation_result_t) == 40u,
               "mutex PI result ABI");

static uint32_t low_stack[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t middle_stack[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t high_stack[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t competitor_stack[STACK_WORDS] __attribute__((aligned(8)));

static hrt_mutex_t mutex_a, mutex_b;
static volatile hrt_mutex_pi_validation_result_t result;
static volatile uint32_t competitor_runs;
static volatile int id_low = -1, id_middle = -1, id_high = -1;

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

__attribute__((noinline, used))
void mutex_pi_validation_emit(const volatile hrt_mutex_pi_validation_result_t *r)
{
    __asm volatile("" : : "r"(r) : "memory");
    for (;;) __asm volatile("wfi");
}

static void finish(uint32_t error)
{
    result.error = error;
    result.competitor_total = competitor_runs;
    result.low_id = (uint32_t)id_low;
    result.middle_id = (uint32_t)id_middle;
    result.high_id = (uint32_t)id_high;
    result.lock_a_owner = (uint32_t)mutex_a.owner;
    result.lock_b_owner = (uint32_t)mutex_b.owner;
    result.passed = error == 0u && result.flags == REQUIRED_FLAGS &&
                    result.competitor_before_handoff == 0u &&
                    competitor_runs == 0u &&
                    mutex_a.owner == HRT_MUTEX_NO_OWNER &&
                    mutex_b.owner == HRT_MUTEX_NO_OWNER;
    mutex_pi_validation_emit(&result);
}

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW 0x58024400UL
#define RCC_GCR (*(volatile uint32_t *)(RCC_BASE_NEW + 0x0u))
#define RCC_GRSTCSETR (*(volatile uint32_t *)(RCC_BASE_NEW + 0x8u))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = 1u;
}

static void competitor_task(void *arg)
{
    (void)arg;
    competitor_runs++;
    /* Sleep rather than endlessly yielding: a broken priority protocol
     * must produce a FAIL result, not starve the low owner forever. */
    hrt_sleep(1000u);
    finish(401u);
}

static void high_task(void *arg)
{
    (void)arg;
    result.flags |= 0x04u;
    if (hrt_mutex_lock(&mutex_a) != 0) finish(301u);
    if (mutex_a.owner != id_high) finish(302u);
    result.flags |= 0x40u;

    if (hrt_mutex_unlock(&mutex_a) != 0) finish(303u);
    result.flags |= 0x80u;
    finish(0u);
}

static void middle_task(void *arg)
{
    (void)arg;
    if (hrt_mutex_lock(&mutex_a) != 0) finish(201u);
    result.flags |= 0x02u;

    const hrt_task_attr_t high = { .priority = HRT_PRIO0, .timeslice = 0u };
    const hrt_task_attr_t medium = { .priority = HRT_PRIO1, .timeslice = 0u };
    id_high = hrt_create_task(high_task, NULL, high_stack, STACK_WORDS, &high);
    if (id_high < 0) finish(202u);
    if (hrt_create_task(competitor_task, NULL,
                        competitor_stack, STACK_WORDS, &medium) < 0) finish(203u);

    hrt_yield();  /* High blocks on A, donating urgency to this owner. */
    result.flags |= 0x08u;
    if (hrt_mutex_lock(&mutex_b) != 0) finish(204u);
    if (mutex_b.owner != id_middle) finish(205u);
    result.flags |= 0x20u;

    if (hrt_mutex_unlock(&mutex_b) != 0) finish(206u);
    if (hrt_mutex_unlock(&mutex_a) != 0) finish(207u);
    for (;;) hrt_yield();
}

static void low_task(void *arg)
{
    (void)arg;
    if (hrt_mutex_lock(&mutex_b) != 0) finish(101u);
    result.flags |= 0x01u;

    const hrt_task_attr_t middle = { .priority = HRT_PRIO2, .timeslice = 0u };
    id_middle = hrt_create_task(middle_task, NULL, middle_stack,
                                STACK_WORDS, &middle);
    if (id_middle < 0) finish(102u);
    hrt_yield();

    /* If transitive inheritance is broken the priority-1 competitor runs
     * before this low-priority owner gets CPU time. */
    result.competitor_before_handoff = competitor_runs;
    if (competitor_runs != 0u) finish(103u);
    result.flags |= 0x10u;
    if (hrt_mutex_unlock(&mutex_b) != 0) finish(104u);
    for (;;) hrt_yield();
}

int main(void)
{
    SystemInit();
    hold_cm4();
    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0u) finish(1u);

    hrt_mutex_init(&mutex_a);
    hrt_mutex_init(&mutex_b);
    const hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY,
        .default_slice = 0u,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_EXTERNAL
    };
    if (hrt_init(&cfg) != HRT_OK) finish(2u);

    const hrt_task_attr_t low = { .priority = HRT_PRIO3, .timeslice = 0u };
    id_low = hrt_create_task(low_task, NULL, low_stack, STACK_WORDS, &low);
    if (id_low < 0) finish(3u);

    hrt_start();
    finish(4u);
    return 1;
}
