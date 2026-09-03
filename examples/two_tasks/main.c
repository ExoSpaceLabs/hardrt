#include "hardrt.h"
#include <stdio.h>

/*
 * POSIX is the hosted functional port. SIGALRM advances HardRT tick accounting,
 * while task context handoff occurs at HardRT scheduling points such as sleep,
 * yield, blocking IPC, task return, or task deletion.
 */

static uint32_t stack_a[2048];
static uint32_t stack_b[2048];

static void taskA(void *arg) {
    (void)arg;
    static uint32_t counter = 0;
    for (;;) {
        printf("[A] tick count [%u]\n", (unsigned)counter);
        fflush(stdout);
        hrt_sleep(500);
        counter++;
    }
}

static void taskB(void *arg) {
    (void)arg;
    for (;;) {
        puts("[B] tock -----");
        fflush(stdout);
        hrt_sleep(1000);
    }
}

int main(void) {
    printf("HardRT version: %s (0x%06X), port: %s (id=%d)\n",
           hrt_version_string(), hrt_version_u32(),
           hrt_port_name(), hrt_port_id());

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_SYSTICK
    };
    if (hrt_init(&cfg) != 0) {
        puts("HardRT init failed");
        return 1;
    }

    const hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    const hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 5 };

    if (hrt_create_task(taskA, NULL, stack_a, sizeof(stack_a) / sizeof(stack_a[0]), &p0) < 0) {
        puts("create taskA failed");
        return 1;
    }
    if (hrt_create_task(taskB, NULL, stack_b, sizeof(stack_b) / sizeof(stack_b[0]), &p1) < 0) {
        puts("create taskB failed");
        return 1;
    }

    hrt_start();
    return 0;
}
