#include "hardrt.h"
#include "hardrt_sem.h"
#include <stdio.h>

static uint32_t sprod[2048], scons[2048];
static hrt_sem_t sem;

static void producer(void *arg) {
    (void)arg;
    for (;;) {
        puts("[P] burst give x3");
        hrt_sem_give(&sem);
        hrt_sem_give(&sem);
        hrt_sem_give(&sem);
        hrt_sleep(300);
    }
}

static void consumer(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sem_take(&sem);
        puts("[C] took token");
        hrt_sleep(200);
    }
}

int main(void) {
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

    hrt_sem_init_counting(&sem, 0, 5);

    const hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    const hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 0 };

    if (hrt_create_task(producer, NULL, sprod, sizeof(sprod) / sizeof(sprod[0]), &p0) < 0) {
        puts("create producer failed");
        return 1;
    }
    if (hrt_create_task(consumer, NULL, scons, sizeof(scons) / sizeof(scons[0]), &p1) < 0) {
        puts("create consumer failed");
        return 1;
    }

    hrt_start();
    return 0;
}
