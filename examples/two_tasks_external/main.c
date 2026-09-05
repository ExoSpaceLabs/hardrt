#include "hardrt.h"
#include "hardrt_time.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static uint32_t stack_a[2048];
static uint32_t stack_b[2048];

static atomic_int g_run_tick_thread = 1;
static uint32_t g_tick_hz = 200;

static void *tick_thread_fn(void *arg) {
    (void)arg;
    struct timespec ts;
    if (g_tick_hz == 0) g_tick_hz = 1000;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000000L / (long)g_tick_hz;

    while (atomic_load(&g_run_tick_thread)) {
        hrt_tick_from_isr();
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void taskA(void *arg) {
    (void)arg;
    static uint32_t counter = 0;
    for (;;) {
        printf("[A] External tick; counter [%u]\n", (unsigned)counter);
        fflush(stdout);
        hrt_sleep(500);
        counter++;
    }
}

static void taskB(void *arg) {
    (void)arg;
    for (;;) {
        puts("[B] External tock -----");
        fflush(stdout);
        hrt_sleep(1000);
    }
}

int main(void) {
    printf("HardRT version: %s (0x%06X), port: %s (id=%d)\n",
           hrt_version_string(), hrt_version_u32(),
           hrt_port_name(), hrt_port_id());

    const hrt_config_t cfg = {
        .tick_hz = g_tick_hz,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_EXTERNAL
    };

    const hrt_status_t init_status = hrt_init(&cfg);
    if (init_status != HRT_OK) {
        printf("HardRT init failed: %d\n", (int)init_status);
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

    pthread_t tick_thread;
    if (pthread_create(&tick_thread, NULL, tick_thread_fn, NULL) != 0) {
        puts("Failed to start tick thread");
        return 1;
    }

    (void)hrt_start();

    atomic_store(&g_run_tick_thread, 0);
    pthread_join(tick_thread, NULL);
    return 0;
}
