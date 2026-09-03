#include <stdint.h>
#include "stm32h7xx.h"

#include "hardrt.h"

/* NUCLEO-H755ZI-Q LEDs:
 * LD1 (green)  = PB0
 * LD2 (yellow) = PE1
 */
#define LED1_GPIO        GPIOB
#define LED1_ENR         RCC_AHB4ENR_GPIOBEN
#define LED1_PIN         0u

#define LED2_GPIO        GPIOE
#define LED2_ENR         RCC_AHB4ENR_GPIOEEN
#define LED2_PIN         1u

volatile uint32_t g_example_error = 0u;
volatile uint32_t dbg_counterA = 0u;
volatile uint32_t dbg_counterB = 0u;

__attribute__((noinline, used))
static void example_fail(uint32_t code)
{
    g_example_error = code;
    __asm volatile("bkpt 0");
    for (;;) __asm volatile("wfi");
}

static inline void hold_cm4(void)
{
#define RCC_BASE_NEW   0x58024400UL
#define RCC_GCR        (*(volatile uint32_t*)(RCC_BASE_NEW + 0x0))
#define RCC_GRSTCSETR  (*(volatile uint32_t*)(RCC_BASE_NEW + 0x8))
    RCC_GCR &= ~(1u << 0);
    RCC_GRSTCSETR = (1u << 0);
}

static void gpio_init(void)
{
    RCC->AHB4ENR |= (LED1_ENR | LED2_ENR);
    __asm volatile("dsb sy");

    LED1_GPIO->MODER &= ~(3u << (LED1_PIN * 2));
    LED1_GPIO->MODER |=  (1u << (LED1_PIN * 2));
    LED1_GPIO->OTYPER &= ~(1u << LED1_PIN);

    LED2_GPIO->MODER &= ~(3u << (LED2_PIN * 2));
    LED2_GPIO->MODER |=  (1u << (LED2_PIN * 2));
    LED2_GPIO->OTYPER &= ~(1u << LED2_PIN);
}

#define STACK_WORDS 512
static uint32_t stackA[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stackB[STACK_WORDS] __attribute__((aligned(8)));

static void TaskA(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_counterA++;
        LED1_GPIO->ODR ^= (1u << LED1_PIN);
        hrt_sleep(250);
    }
}

static void TaskB(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_counterB++;
        LED2_GPIO->ODR ^= (1u << LED2_PIN);
        hrt_sleep(500);
    }
}

int main(void)
{
    SystemInit();
    hold_cm4();
    gpio_init();

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };

    if (hrt_init(&cfg) != 0) example_fail(1u);

    const hrt_task_attr_t tA = { .priority = HRT_PRIO0, .timeslice = 5 };
    const hrt_task_attr_t tB = { .priority = HRT_PRIO1, .timeslice = 5 };

    if (hrt_create_task(TaskA, 0, stackA, STACK_WORDS, &tA) < 0) example_fail(2u);
    if (hrt_create_task(TaskB, 0, stackB, STACK_WORDS, &tB) < 0) example_fail(3u);

    hrt_start();
    example_fail(4u);
    return 1;
}
