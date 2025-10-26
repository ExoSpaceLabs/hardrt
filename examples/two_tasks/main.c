#include "heartos.h"
#include <stdio.h>

/* This example compiles and “runs,” but with the null port there’s no real
   preemption or tick. It just shows init and task creation without exploding.
   Once you add a real port (posix or cortex_m), these tasks will actually run. */

static uint32_t stack_a[256];
static uint32_t stack_b[256];

static void taskA(void* arg){
    (void)arg;
    for(;;){
        // Placeholder work
        // In a real port, you’d do UART TX or blink
        hrt_sleep(500);
    }
}

static void taskB(void* arg){
    (void)arg;
    for(;;){
        hrt_sleep(1000);
    }
}

int main(void){
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy  = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5
    };
    if (hrt_init(&cfg) != 0){
        puts("HeaRTOS init failed");
        return 1;
    }

    hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 5 };

    if (hrt_create_task(taskA, 0, stack_a, 256, &p0) < 0) puts("create taskA failed");
    if (hrt_create_task(taskB, 0, stack_b, 256, &p1) < 0) puts("create taskB failed");

    puts("HeaRTOS example compiled. With null port there is no live scheduling yet.");
    // hrt_start(); // uncomment once you add a real port
    return 0;
}
