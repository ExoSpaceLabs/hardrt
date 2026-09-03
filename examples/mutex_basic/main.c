#include "hardrt.h"
#include "hardrt_mutex.h"
#include <stdio.h>

static uint32_t sa[2048], sb[2048];
static hrt_mutex_t mutex;

static void A(void *arg) {
    (void)arg;
    for (;;) {
        puts("[A] waiting for mutex");
        if (hrt_mutex_lock(&mutex) == 0) {
            puts("[A] locked mutex");
            hrt_sleep(300);
            puts("[A] unlocking mutex");
            hrt_mutex_unlock(&mutex);
        }
        hrt_sleep(200);
    }
}

static void B(void *arg) {
    (void)arg;
    for (;;) {
        if (hrt_mutex_try_lock(&mutex) == 0) {
            puts("[B] got mutex via try_lock");
            hrt_sleep(150);
            hrt_mutex_unlock(&mutex);
        } else {
            puts("[B] mutex busy, waiting");
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

    hrt_mutex_init(&mutex);
    const hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 5 };
    const hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 5 };

    if (hrt_create_task(A, NULL, sa, sizeof(sa) / sizeof(sa[0]), &p0) < 0) {
        puts("create A failed");
        return 1;
    }
    if (hrt_create_task(B, NULL, sb, sizeof(sb) / sizeof(sb[0]), &p1) < 0) {
        puts("create B failed");
        return 1;
    }

    puts("Starting mutex basic example...");
    hrt_start();
    return 0;
}
