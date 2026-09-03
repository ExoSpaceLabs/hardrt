#include "hardrt.h"
#include "hardrt_sem.h"
#include <stdio.h>

static uint32_t sa[2048], sb[2048];
static hrt_sem_t sem;

static void A(void *arg) {
    (void)arg;
    for (;;) {
        hrt_sem_take(&sem);
        puts("[A] got sem");
        hrt_sleep(200);
        hrt_sem_give(&sem);
        hrt_sleep(200);
    }
}

static void B(void *arg) {
    (void)arg;
    for (;;) {
        if (hrt_sem_try_take(&sem) == 0) {
            puts("[B] got sem");
            hrt_sleep(100);
            hrt_sem_give(&sem);
        } else {
            puts("[B] waiting");
        }
        hrt_sleep(100);
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

    hrt_sem_init(&sem, 1);
    const hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    const hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 5 };

    if (hrt_create_task(A, NULL, sa, sizeof(sa) / sizeof(sa[0]), &p0) < 0) {
        puts("create A failed");
        return 1;
    }
    if (hrt_create_task(B, NULL, sb, sizeof(sb) / sizeof(sb[0]), &p1) < 0) {
        puts("create B failed");
        return 1;
    }

    hrt_start();
    return 0;
}
