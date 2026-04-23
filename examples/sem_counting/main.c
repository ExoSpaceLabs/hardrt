#include "hardrt.h"
#include "hardrt_sem.h"
#include <stdio.h>

static uint32_t sprod[2048], scons[2048];
static hrt_sem_t sem;

/* Producer: generates bursts of tokens. */
static void producer(void*) {
    for (;;) {
        puts("[P] burst give x3");
        hrt_sem_give(&sem);
        hrt_sem_give(&sem);
        hrt_sem_give(&sem);
        hrt_sleep(300);
    }
}

/* Consumer: slower than producer so tokens can accumulate (up to max_count). */
static void consumer(void*) {
    for (;;) {
        hrt_sem_take(&sem);
        puts("[C] took token");
        hrt_sleep(200);
    }
}

int main() {
    const hrt_config_t cfg = { .tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5 };
    hrt_init(&cfg);

    /* Counting semaphore: start empty, saturate at 5 tokens. */
    hrt_sem_init_counting(&sem, 0, 5);

    const hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    const hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 0 };

    if (hrt_create_task(producer, NULL, sprod, 2048, &p0) < 0) puts("create producer failed");
    if (hrt_create_task(consumer, NULL, scons, 2048, &p1) < 0) puts("create consumer failed");

    hrt_start();
    return 0;
}