#include <stdint.h>
#include "hardrt.h"
#include "stm32h7xx.h"

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

#define STACK_WORDS 512
static uint32_t stackA[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stackB[STACK_WORDS] __attribute__((aligned(8)));

volatile uint32_t g_example_error = 0u;
volatile uint32_t dbg_counterA = 0u;
volatile uint32_t dbg_counterB = 0u;
volatile uint32_t dbg_exit_counterA = 0u;
volatile uint32_t dbg_exit_counterB = 0u;

__attribute__((noinline, used))
static void example_fail(uint32_t code)
{
    g_example_error = code;
    __asm volatile("bkpt 0");
    for (;;) __asm volatile("wfi");
}

static void TaskA(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_counterA++;
        hrt_sleep(500);
        dbg_exit_counterA++;
    }
}

static void TaskB(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_counterB++;
        hrt_sleep(1000);
        dbg_exit_counterB++;
    }
}

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW 0x58024400UL
#define RCC_GCR (*(volatile uint32_t*)(RCC_BASE_NEW + 0x0))
#define RCC_GRSTCSETR (*(volatile uint32_t*)(RCC_BASE_NEW + 0x8))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

int main(void)
{
    SystemInit();
    hold_cm4();

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };
    if (hrt_init(&cfg) != 0) example_fail(1u);

    const hrt_task_attr_t a0 = { .priority = HRT_PRIO0, .timeslice = 5 };
    const hrt_task_attr_t a1 = { .priority = HRT_PRIO1, .timeslice = 5 };
    if (hrt_create_task(TaskA, 0, stackA, sizeof(stackA) / sizeof(stackA[0]), &a0) < 0) example_fail(2u);
    if (hrt_create_task(TaskB, 0, stackB, sizeof(stackB) / sizeof(stackB[0]), &a1) < 0) example_fail(3u);

    hrt_start();
    example_fail(4u);
    return 1;
}
