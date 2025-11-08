#include <stdint.h>
#include "heartos.h"
#include "heartos_time.h"

// if not defined by system stub it here
// void ExitRun0Mode(void) __attribute__((weak));
// void ExitRun0Mode(void) {}

extern void SystemInit(void);
extern uint32_t SystemCoreClock;

static uint32_t stackA[512], stackB[512];

static void TaskA(void* arg) { (void)arg; for(;;) hrt_sleep(500); }
static void TaskB(void* arg) { (void)arg; for(;;) hrt_sleep(1000); }


static inline void hold_cm4(void){
#define RCC_BASE 0x58024400UL
#define RCC_GCR (*(volatile uint32_t*)(RCC_BASE + 0x0))
#define RCC_GRSTCSETR (*(volatile uint32_t*)(RCC_BASE + 0x8))
    RCC_GCR &= ~(1u<<0);         // don't boot CM4
    RCC_GRSTCSETR = (1u<<0);     // assert CM4 reset
}

int main(void){
    SystemInit();
    hold_cm4(); // belt-and-suspenders

    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy  = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK
    };
    hrt_init(&cfg);

    hrt_task_attr_t a0 = { .priority=HRT_PRIO0, .timeslice=0 };
    hrt_task_attr_t a1 = { .priority=HRT_PRIO1, .timeslice=5 };
    hrt_create_task(TaskA, 0, stackA, 512, &a0);
    hrt_create_task(TaskB, 0, stackB, 512, &a1);

    hrt_start();
    for(;;) __asm volatile("wfi");
}
