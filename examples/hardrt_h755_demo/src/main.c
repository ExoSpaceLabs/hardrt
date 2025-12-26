#include <stdbool.h>
#include <stdint.h>
#include "hardrt.h"
#include "hardrt_time.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"


#define HRT_STACK_SIZE 1024

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

static uint32_t stackA[1024], stackB[1024];

volatile uint32_t dbg_counterA = 0;
volatile uint32_t dbg_counterB = 0;
volatile uint32_t dbg_exit_counterA = 0;
volatile uint32_t dbg_exit_counterB = 0;

static void TaskA(void* arg) {
    (void)arg;
    for(;;) {
        dbg_counterA++;
        (void)dbg_counterA;
        hrt_sleep(5000);
        dbg_exit_counterA++;
        (void)dbg_exit_counterA;
    }
}
static void TaskB(void* arg) {
    (void)arg;
    for(;;) {
        dbg_counterB++;
        (void)dbg_counterB;
        hrt_sleep(10000);
        dbg_exit_counterB++;
        (void)dbg_exit_counterB;
    }
}


static inline void hold_cm4(void){
#define RCC_BASE_NEW 0x58024400UL
#define RCC_GCR (*(volatile uint32_t*)(RCC_BASE_NEW + 0x0))
#define RCC_GRSTCSETR (*(volatile uint32_t*)(RCC_BASE_NEW + 0x8))
    RCC_GCR &= ~(1u<<0);         // don't boot CM4
    RCC_GRSTCSETR = (1u<<0);     // assert CM4 reset
}

int main(void){
    SystemInit();
    hold_cm4();

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy  = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };
    hrt_init(&cfg);

    hrt_task_attr_t a0 = { .priority=HRT_PRIO0, .timeslice=5 };
    hrt_task_attr_t a1 = { .priority=HRT_PRIO1, .timeslice=5 };
    hrt_create_task(TaskA, 0, stackA, sizeof(stackA)/sizeof(stackA[0]), &a0);
    hrt_create_task(TaskB, 0, stackB, sizeof(stackB)/sizeof(stackB[0]), &a1);

    hrt_start();
    //for(;;) __asm volatile("wfi");
    return 1;
}
