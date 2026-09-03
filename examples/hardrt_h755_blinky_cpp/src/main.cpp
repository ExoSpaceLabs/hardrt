#include <stdint.h>
#include "stm32h7xx.h"
#include "hardrtpp.hpp"

#define LED1_GPIO        GPIOB
#define LED1_ENR         RCC_AHB4ENR_GPIOBEN
#define LED1_PIN         0u

#define LED2_GPIO        GPIOE
#define LED2_ENR         RCC_AHB4ENR_GPIOEEN
#define LED2_PIN         1u

using namespace hardrt;

extern "C" {
volatile uint32_t g_example_error = 0u;
volatile uint32_t dbg_counterA = 0u;
volatile uint32_t dbg_counterB = 0u;
}

extern "C" __attribute__((noinline, used))
void example_fail(uint32_t code)
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

static void TaskA(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_counterA++;
        LED1_GPIO->ODR ^= (1u << LED1_PIN);
        Task::sleep(100);
    }
}

static void TaskB(void *arg)
{
    (void)arg;
    for (;;) {
        dbg_counterB++;
        LED2_GPIO->ODR ^= (1u << LED2_PIN);
        Task::sleep(250);
    }
}

extern "C" int main(void)
{
    SystemInit();
    hold_cm4();
    gpio_init();

    const hrt_config_t cfg = {
        1000,
        HRT_SCHED_PRIORITY_RR,
        5,
        SystemCoreClock,
        HRT_TICK_SYSTICK
    };

    if (System::init(cfg) != 0) example_fail(1u);
    if (Task::create<512, 0>(TaskA, nullptr, HRT_PRIO0, 5) < 0) example_fail(2u);
    if (Task::create<512, 1>(TaskB, nullptr, HRT_PRIO1, 5) < 0) example_fail(3u);

    System::start();
    example_fail(4u);
    return 1;
}
