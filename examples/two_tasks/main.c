#include "heartos.h"
#include <stdio.h>

/* With the POSIX port, this will actually run and preempt via SIGALRM. */

static uint32_t stack_a[2048]; // bigger stacks are safer on glibc
static uint32_t stack_b[2048];


static void taskA(void* arg){
    (void)arg;
    static uint32_t counter = 0;
    for(;;){
        printf("[A] tick count [%d]\n",counter);
        fflush(stdout);
        hrt_sleep(500);
        counter++;
    }
}

static void taskB(void* arg){
    (void)arg;
    for(;;){
        puts("[B] tock -----");
        fflush(stdout);
        hrt_sleep(1000);
    }
}

int main(void){
    printf("HeaRTOS version: %s (0x%06X), port: %s (id=%d)\n",
       hrt_version_string(), hrt_version_u32(),
       hrt_port_name(), hrt_port_id());


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

    if (hrt_create_task(taskA, 0, stack_a, sizeof(stack_a)/sizeof(stack_a[0]), &p0) < 0)
        puts("create taskA failed");
    if (hrt_create_task(taskB, 0, stack_b, sizeof(stack_b)/sizeof(stack_b[0]), &p1) < 0)
        puts("create taskB failed");

    /* Run the scheduler. This never returns on the POSIX port. */
    hrt_start();

    /* Not reached */
    return 0;
}
