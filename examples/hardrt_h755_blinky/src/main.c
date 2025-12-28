#include <stdint.h>
#include "stm32h7xx.h"

#include "hardrt.h"
#include "hardrt_time.h"

/* NUCLEO-H755ZI-Q LEDs:
 * LD1 (green)  = PB0
 * LD2 (yellow) = PE1
 * LD3 (red)    = PB14
 */
#define LED1_GPIO        GPIOB
#define LED1_ENR         RCC_AHB4ENR_GPIOBEN
#define LED1_PIN         0u    /* PB0 */

#define LED2_GPIO        GPIOE
#define LED2_ENR         RCC_AHB4ENR_GPIOEEN
#define LED2_PIN         1u    /* PE1 */

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
    __asm volatile ("dsb sy");

    /* PB0 output */
    LED1_GPIO->MODER &= ~(3u << (LED1_PIN * 2));
    LED1_GPIO->MODER |=  (1u << (LED1_PIN * 2));
    LED1_GPIO->OTYPER &= ~(1u << LED1_PIN);

    /* PE1 output */
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
        LED1_GPIO->ODR ^= (1u << LED1_PIN);  /* LD1 */
        hrt_sleep(250);
    }
}

static void TaskB(void *arg)
{
    (void)arg;
    for (;;) {
        LED2_GPIO->ODR ^= (1u << LED2_PIN);  /* LD2 */
        hrt_sleep(500);
    }
}

int main(void)
{
    SystemInit();
    hold_cm4();
    gpio_init();

    hrt_config_t cfg = {
        .tick_hz        = 1000,
        .policy         = HRT_SCHED_PRIORITY_RR,
        .default_slice  = 5,
        .core_hz        = SystemCoreClock,
        .tick_src       = HRT_TICK_SYSTICK
    };

    hrt_init(&cfg);

    hrt_task_attr_t tA = { .priority = HRT_PRIO0, .timeslice = 5 };
    hrt_task_attr_t tB = { .priority = HRT_PRIO1, .timeslice = 5 };

    hrt_create_task(TaskA, 0, stackA, STACK_WORDS, &tA);
    hrt_create_task(TaskB, 0, stackB, STACK_WORDS, &tB);

    hrt_start();
    return 0;
}
